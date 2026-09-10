<script>
  import { onMount, tick } from 'svelte';
  import { openStore } from '../../client/store.mjs';
  import { assemble, beautifySource, fantamsVersion, parseDirectives, upsertDirectives, includePath } from '../../wasm/assemble.mjs';
  import { makeEditor, makeViewer } from './lib/editor.js';
  import { ANSI, severity, parseSourceRef, matchInclude } from './lib/diagnostics.js';
  import { ping as pingAmspirit, injectMedia } from './lib/amspirit.js';

  let store = $state(null);
  let mode = $state('…');
  let count = $state(0);
  let sources = $state([]);
  let query = $state('');
  let selected = $state(null);
  let asm = $state(''), buildmode = $state('sna'), entry = $state('');
  // Profil fantams (cpc6128/cpcplus), script de lien (.ld, reference vers un include) et
  // conteneur : reglages de Projet (CONTEXT.md), jamais reflete dans la directive ;z80:
  // (docs/adr/0001-...). '' = auto/absent.
  let profile = $state(''), ldFilename = $state(''), container = $state('sna');
  // Banque physique de cartouche (0..31) qu'un export .cpr de CETTE source seule
  // doit produire (fantams --cpr-bank). Ephemere (pas en base) : l'affectation
  // qui compte pour un Projet CPR complet vit dans project_banks (voir plus bas) —
  // ceci ne sert qu'a tester une banque isolement, sans Projet.
  let cprBank = $state(0);
  const isFantams = $derived(!asm || asm === 'fantams');
  // Base (ADR 0012) : le snapshot post-boot sur lequel l'assemblage est pose, pour
  // qu'un code appelant le firmware s'execute. '' = aucune (la memoire vaut zero).
  let base = $state(''), bases = $state([]);
  let isInclude = $state(false), incFilename = $state(''); // fichier librairie (sans point d'entrée), injecté dans le FS wasm des autres sources
  // Liste des sources is_include=1 ({id,name,filename,code}), rafraîchie à chaque run()/beautify().
  // $state (et non une simple variable) : le sélecteur de script de lien (ldOptions,
  // popup de réglages) doit se mettre à jour dès qu'elle est peuplée.
  let includeCache = $state(null);
  // Scripts de lien disponibles : les includes dont le nom finit en .ld (convention —
  // les autres sont des librairies .asm).
  const ldOptions = $derived((includeCache || []).filter((i) => /\.ld$/i.test(i.filename || '')));
  let logLines = $state([]);
  // Journal d'assemblage : logé sous l'éditeur par défaut, mais déplacé dans la colonne de
  // l'émulateur quand celle-ci est vide (échec d'assemblage) ou sur demande (bouton 🗒),
  // pour lire les erreurs sans rogner l'éditeur.
  let logBig = $state(false);
  let logH = $state(120);            // hauteur (px) du journal quand il reste sous l'éditeur
  let showWarnings = $state(true);   // les avertissements sont masquables : sur une source qui en
                                     // crache trente, ils noient les deux lignes qui comptent
  let lineOffset = 0; // nb de lignes d'en-tête injectées avant le code utilisateur par le dernier build
  // Identité de la source ASSEMBLÉE par le dernier run(). Le journal peut survivre à un
  // changement de source (on ouvre l'include fautif sans réassembler) : sans ça, une ligne
  // pointant sur le fichier principal n'aurait plus rien à quoi revenir.
  let mainId = null, mainName = $state('');
  // Historique des sources ouvertes : [{ id, name, line }], `line` = où était le curseur en
  // partant, pour revenir à l'endroit quitté et pas en tête de fichier.
  let history = $state([]);
  let pendingNav = $state(null); // { ref, back } — navigation suspendue à une confirmation
  let emuUrl = $state('');
  let busy = $state(false);
  let dlUrl = $state('');
  let dlExt = $state('sna');
  let editor, editorEl;
  let showList = $state(true);         // panneau latéral (liste des sources) affiché/masqué
  let listEl;                         // <aside class="list"> — seul conteneur défilant des vignettes
  let savedListScroll = 0;            // position de défilement à préserver pendant un lancement
  // Restaure la position des vignettes : le chargement de l'émulateur (focus du canvas dans
  // l'iframe) fait sinon remonter la liste tout en haut. Appelé après le build et au load iframe.
  function restoreListScroll() { if (listEl) listEl.scrollTop = savedListScroll; }
  let preText = $state('');           // source après préprocesseur (en-tête/footer injectés)
  let showPre = $state(false);
  let preEl, preViewer;

  const canWrite = $derived(store?.canWrite ?? false);
  const dlName = $derived((selected?.name || 'build').replace(/[^\w.-]+/g, '_') + '.' + dlExt);

  // Genres (taxonomie contrôlée, éditable par source).
  const GENRES = ['disassembled game', 'demo', 'graphics', 'audio', 'asm test', 'tools', 'math', 'other'];

  // Regroupement hiérarchique de la liste.
  let groupBy = $state('');            // '' | author | group_name | buildmode | assembler | genre
  let collapsed = $state({});
  const GROUP_LABEL = { author: 'unknown author', group_name: '(no group)', buildmode: '(type ?)', assembler: 'rasm (default)', genre: '(unclassified)' };

  // Les librairies (is_include) forment toujours leur propre groupe, en queue de liste, quel
  // que soit le critère : ce ne sont pas des programmes, on ne les ouvre pas pour les lancer.
  // Réparties par auteur ou par genre, elles diluaient les sources exécutables — et elles
  // sont nombreuses. En bloc et repliable, elles se sortent du chemin d'un seul clic.
  const LIB_GROUP = '🧩 libraries';

  const groups = $derived.by(() => {
    const libs = sources.filter((s) => s.is_include);
    const rest = sources.filter((s) => !s.is_include);
    const tail = libs.length ? [{ key: LIB_GROUP, items: libs }] : [];
    if (!groupBy) return [...(rest.length ? [{ key: null, items: rest }] : []), ...tail];
    const map = new Map();
    for (const s of rest) {
      const k = s[groupBy] || GROUP_LABEL[groupBy] || '(vide)';
      if (!map.has(k)) map.set(k, []);
      map.get(k).push(s);
    }
    return [...[...map.entries()]
      .sort((a, b) => String(a[0]).localeCompare(String(b[0]), 'en'))
      .map(([key, items]) => ({ key, items })), ...tail];
  });
  const toggle = (k) => { collapsed = { ...collapsed, [k]: !collapsed[k] }; };

  // Popup de réglages de la source (libère la barre de l'éditeur).
  let showSettings = $state(false);
  const openSettings = async () => {
    showSettings = true;
    // Le sélecteur de script de lien a besoin de la liste des includes : la charger ici
    // couvre le cas où les réglages sont ouverts avant tout run()/beautify().
    if (!includeCache) { try { includeCache = await store.listIncludes(); } catch { includeCache = []; } }
  };
  async function applySettings() {
    showSettings = false;
    const it = selected?.id ? sources.find((s) => s.id === selected.id) : null;
    if (it && selected) {
      it.name = selected.name; it.author = selected.author; it.genre = selected.genre;
      it.assembler = asm || null; it.buildmode = buildmode; it.is_include = isInclude ? 1 : 0;
      it.profile = profile || null; it.ld_filename = ldFilename || null; it.container = container || null;
    }
    if (canWrite && selected?.id) {
      try {
        await store.update(selected.id, {
          name: selected.name, author: selected.author, description: selected.description,
          genre: selected.genre, assembler: asm || null, buildmode, entry_point: entry || null,
          is_include: isInclude ? 1 : null, filename: isInclude ? (incFilename || null) : null,
          profile: isInclude ? null : (profile || null),
          ld_filename: isInclude ? null : (ldFilename || null),
          container: isInclude ? null : (container || null),
        });
      } catch {}
    }
  }

  // Réglages globaux de l'app : émulateur wasm intégré (défaut) vs AMSpiriT externe piloté
  // par son API HTTP locale. Quand AMSpiriT est joignable, le panel wasm se masque et
  // l'exécution passe par injection RAM + PC via l'API au lieu du service worker.
  const SETTINGS_KEY = 'z80live.settings';
  let showAppSettings = $state(false);
  let amspiritEnabled = $state(false);
  let amspiritUrl = $state('http://127.0.0.1:8765');
  let amspiritConnected = $state(false);
  const emuCol = $derived(!(amspiritEnabled && amspiritConnected)); // la colonne de droite existe-t-elle ?
  const logAside = $derived(emuCol && (logBig || !emuUrl));  // journal déplacé à droite
  let amspiritTimer;

  function loadSettings() {
    try {
      const s = JSON.parse(localStorage.getItem(SETTINGS_KEY) || '{}');
      amspiritEnabled = !!s.amspiritEnabled;
      amspiritUrl = s.amspiritUrl || amspiritUrl;
    } catch {}
  }
  function saveSettings() {
    try { localStorage.setItem(SETTINGS_KEY, JSON.stringify({ amspiritEnabled, amspiritUrl })); } catch {}
  }
  // Sonde périodique de connectivité (uniquement si AMSpiriT est activé dans les réglages).
  function syncAmspiritPolling() {
    clearInterval(amspiritTimer); amspiritTimer = null;
    if (!amspiritEnabled) { amspiritConnected = false; return; }
    const check = async () => { amspiritConnected = await pingAmspirit(amspiritUrl); };
    check();
    amspiritTimer = setInterval(check, 3000);
  }
  const openAppSettings = () => { showAppSettings = true; };
  function closeAppSettings() { showAppSettings = false; saveSettings(); syncAmspiritPolling(); }

  // ---- Projets CPR (db/schema.sql : `projects` + `project_banks`) ----
  // Un Projet regroupe des Sources, une par banque physique (0..31) de cartouche
  // CPC+. Chaque banque reste liee SEPAREMENT (fantams/cpr.h) : "construire" un
  // Projet, c'est rappeler assemble() une fois par banque, dans l'ordre, en
  // faisant voyager le .cpr accumule d'un appel a l'autre (wasm/assemble.mjs,
  // `prevCpr`) — exactement ce que `fantams -o x.cpr --cpr-bank n` fait sur la CLI.
  let showProjects = $state(false);
  let projects = $state([]);          // resume [{id,name,bank_count}]
  let currentProject = $state(null);  // detail ouvert {id,name,banks:[{bank,source_id,source_name}]}
  let newBankNumber = $state(0);
  let projectBusy = $state(false);
  let projectLog = $state([]);
  let projectDlUrl = $state('');

  async function openProjects() {
    showProjects = true;
    try { projects = await store.listProjects(); } catch { projects = []; }
  }
  function closeProjects() { showProjects = false; currentProject = null; projectLog = []; }
  async function openProject(id) {
    projectLog = []; if (projectDlUrl) URL.revokeObjectURL(projectDlUrl); projectDlUrl = '';
    try { currentProject = await store.getProject(id); } catch { currentProject = null; }
  }
  async function createProjectPrompt() {
    const p = await store.createProject({ name: 'untitled project' });
    projects = await store.listProjects();
    await openProject(p.id);
  }
  async function renameCurrentProject(name) {
    if (!currentProject) return;
    currentProject = await store.updateProject(currentProject.id, { name });
    projects = await store.listProjects();
  }
  async function deleteProject(id) {
    await store.removeProject(id);
    if (currentProject?.id === id) currentProject = null;
    projects = await store.listProjects();
  }
  // Affecte la source OUVERTE dans l'editeur a la banque `newBankNumber` du
  // Projet courant : pas de second selecteur de source a maintenir, l'editeur
  // en porte deja un.
  async function addCurrentAsBank() {
    if (!currentProject || !selected?.id) return;
    currentProject = await store.setProjectBank(currentProject.id, Number(newBankNumber), selected.id);
    projects = await store.listProjects();
  }
  async function removeBank(bank) {
    if (!currentProject) return;
    currentProject = await store.removeProjectBank(currentProject.id, bank);
  }

  // Construit le .cpr complet : une banque a la fois, dans l'ORDRE des numeros
  // de banque, en faisant voyager les octets deja accumules (prevCpr). Une
  // seule banque en echec arrete tout — un .cpr partiel qui SE FAIT PASSER pour
  // complet serait pire que rien (meme raison que cpr.cpp cote CLI).
  async function buildProject() {
    if (!currentProject || currentProject.banks.length === 0) return;
    projectBusy = true; projectLog = [];
    if (projectDlUrl) URL.revokeObjectURL(projectDlUrl);
    projectDlUrl = '';
    const plog = (m) => { projectLog = [...projectLog, m]; };
    try {
      const includes = await store.listIncludes().catch(() => []);
      let acc = null;
      const banks = [...currentProject.banks].sort((a, b) => a.bank - b.bank);
      for (const b of banks) {
        const src = await store.get(b.source_id);
        if (!src) { plog(`❌ bank ${b.bank}: source ${b.source_id} introuvable`); projectBusy = false; return; }
        plog(`banque ${b.bank} (${src.name})…`);
        const res = await assemble({
          code: src.code, assembler: 'fantams', profile: src.profile || undefined,
          ldFile: src.ld_filename || undefined, container: 'cpr', cprBank: b.bank,
          prevCpr: acc || undefined, includes: includes.filter((i) => i.id !== src.id),
        }, factories);
        if (!res.ok || !res.output) {
          plog(`❌ banque ${b.bank} (${src.name}) — echec`);
          for (const l of res.log.slice(-12)) plog('   ' + l);
          if (res.error) plog('   ⚠ ' + res.error);
          projectBusy = false; return;
        }
        acc = res.output;
        plog(`✔ banque ${b.bank} — ${acc.length} o au total`);
      }
      projectDlUrl = URL.createObjectURL(new Blob([acc], { type: 'application/octet-stream' }));
      plog(`✔ ${currentProject.name}.cpr — ${banks.length} banque(s), ${acc.length} octets`);
      if (amspiritEnabled && amspiritConnected) {
        try {
          const name = (currentProject.name || 'project').replace(/[^\w.-]+/g, '_') + '.cpr';
          await injectMedia(amspiritUrl, acc, { name, onLog: plog });
          plog('✔ injected into AMSpiriT');
        } catch (e) {
          amspiritConnected = false;
          plog('⚠ AMSpiriT injection failed: ' + (e?.message || e));
        }
      }
    } catch (e) {
      plog('Error: ' + (e?.message || e));
    }
    projectBusy = false;
  }

  // Réassemblage automatique à chaque modification (debounce 500 ms).
  let auto = $state(false);
  let dirty = $state(false);          // modifs non sauvegardées depuis le dernier load/save
  let autoTimer, suppressEdit = false;
  // Écriture programmatique de l'éditeur qui ne doit PAS relancer l'auto-assemblage (sinon boucle).
  const setCode = (v) => { suppressEdit = true; editor.value = v; suppressEdit = false; };
  function onEdit() {
    if (suppressEdit) return;
    dirty = true;
    if (!auto) return;
    clearTimeout(autoTimer); autoTimer = setTimeout(() => { if (!busy) run(); }, 500);
  }
  function onAutoToggle() { if (auto && !busy) run(); }

  // Réinstancie un Module WASM neuf à chaque assemblage (rasm/sjasmplus appellent exit()).
  // URL construite dynamiquement -> non analysée par le bundler, chargée depuis /wasm (public/).
  const loadWasm = (name) => import(/* @vite-ignore */ new URL('/wasm/' + name, location.origin).href);
  // Catalogue des bases : servi statiquement depuis public/bases/. L'ID de la
  // directive (`base=cpc6128-en`) est resolu ICI — fantams ne voit qu'un chemin.
  const baseUrl = (name) => new URL('/bases/' + name, location.origin).href;
  const baseCache = new Map();
  (async () => {
    try { bases = await (await fetch(baseUrl('index.json'))).json(); } catch { bases = []; }
  })();
  async function resolveBase(id) {
    if (baseCache.has(id)) return baseCache.get(id);
    const entryDef = (bases || []).find((b) => b.id === id);
    if (!entryDef) return null;
    try {
      const r = await fetch(baseUrl(entryDef.file));
      if (!r.ok) return null;
      const bytes = new Uint8Array(await r.arrayBuffer());
      baseCache.set(id, bytes);
      return bytes;
    } catch { return null; }
  }

  const factories = {
    createRasm: async (o) => (await loadWasm('rasm.mjs')).default(o),
    createSjasm: async (o) => (await loadWasm('sjasmplus.mjs')).default(o),
    createFantams: async (o) => (await loadWasm('fantams.mjs')).default(o),
  };

  // La version de fantams que CET artefact-la porte. Quand une verification
  // echouera, la premiere question sera « quel fantams ? », et elle se posera
  // depuis le navigateur — c'est la que la peremption a dure trois etages sans
  // etre vue.
  //
  // Elle est affichee TELLE QUELLE : rien ne la decoupe, rien ne la compare,
  // aucun ordre entre versions n'est defini. Un changement de sa forme ne doit
  // rien casser ici.
  let fantamsVer = $state('');

  // Traduit le fichier cité par un diagnostic en SOURCE de la base. Les librairies sont
  // écrites dans le FS wasm sous includePath(), le fichier principal sous '/in.asm' — c'est
  // le seul à porter l'en-tête injecté, donc le seul auquel `lineOffset` s'applique. Un
  // fichier inconnu retombe sur la source courante, comme avant.
  function resolveRef(text) {
    const r = parseSourceRef(text);
    if (!r) return null;
    const inc = matchInclude(r.file, includeCache, includePath);
    if (inc) return { srcId: inc.id, name: inc.name, line: r.line, file: r.file };
    return { srcId: mainId, name: mainName, line: r.line - lineOffset, file: r.file };
  }

  // `ref` : { srcId, name, line, file } déjà recalé sur les lignes de l'ÉDITEUR (resolveRef a
  // retiré l'en-tête injecté). Une ligne <= 0 désigne l'en-tête lui-même, que l'auteur n'a pas
  // écrit : elle n'est pas cliquable.
  const log = (m, cls = '', ref = null) => {
    logLines = [...logLines, { m: m.replace(ANSI, ''), cls, ref: ref && ref.line > 0 ? ref : null }];
  };

  // Le compte ne porte que sur les diagnostics de l'ASSEMBLEUR ('err'/'warn'). Les échecs
  // propres à l'application (injection, service worker) sont en 'fail' : rouges eux aussi,
  // mais hors décompte — annoncer « 2 erreurs » pour une seule erreur de source et son
  // résumé ferait chercher au lecteur une deuxième ligne qui n'existe pas.
  const errCount  = $derived(logLines.filter((l) => l.cls === 'err').length);
  const warnCount = $derived(logLines.filter((l) => l.cls === 'warn').length);
  // Le filtre ne touche QUE les avertissements : masquer une erreur laisserait un journal
  // qui ment sur l'échec qu'il rapporte.
  const shownLines = $derived(showWarnings ? logLines : logLines.filter((l) => l.cls !== 'warn'));

  async function initSW() {
    if (!('serviceWorker' in navigator)) return;
    try { await navigator.serviceWorker.register('/emu-sw.js'); await navigator.serviceWorker.ready; } catch {}
  }

  async function loadLocalDb() {
    await new Promise((ok, ko) => {
      const s = document.createElement('script');
      s.src = '/vendor/sqljs/sql-wasm.js'; s.onload = ok; s.onerror = ko; document.head.appendChild(s);
    });
    const SQL = await window.initSqlJs({ locateFile: (f) => '/vendor/sqljs/' + f });
    const gz = await (await fetch('/db/z80live.sqlite.gz')).arrayBuffer();
    const buf = await new Response(new Blob([gz]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
    return { db: new SQL.Database(new Uint8Array(buf)), hasFts: false };
  }

  function feedEmulator(bytes, ext) {
    return new Promise((resolve) => {
      const name = 'b' + Date.now() + '.' + ext;
      const ctrl = navigator.serviceWorker?.controller;
      if (!ctrl) return resolve(null);
      const onMsg = (e) => {
        if (e.data?.type === 'build-ready' && e.data.name === name) {
          navigator.serviceWorker.removeEventListener('message', onMsg);
          resolve('/build/' + name);
        }
      };
      navigator.serviceWorker.addEventListener('message', onMsg);
      ctrl.postMessage({ type: 'put-build', name, bytes });
    });
  }

  let searchTimer;
  function onSearch() { clearTimeout(searchTimer); searchTimer = setTimeout(refreshList, 250); }
  async function refreshList() {
    if (!store) return;
    sources = await store.list({ q: query || undefined, buildmode: 'sna', limit: 300 });
  }

  // `run` : assembler après l'ouverture (le geste « je choisis cette source »). Une navigation
  // depuis le journal passe run:false — réassembler effacerait justement le journal qui a
  // envoyé le lecteur ici. `line` place le curseur, `push` empile la source quittée.
  async function selectSource(id, { run: doRun = true, line = null, push = true } = {}) {
    if (push && selected?.id && selected.id !== id) {
      const prev = { id: selected.id, name: selected.name, line: editor?.cursorLine || 1 };
      history = [...history, prev].slice(-20);
    }
    const s = await store.get(id);
    selected = s;
    setCode(s.code || '');
    dirty = false;
    const d = parseDirectives(s.code || '');
    asm = d.assembler || s.assembler || '';
    const bm = d.buildmode || s.buildmode || 'sna';
    buildmode = bm.startsWith('sna') ? (bm === 'sna' ? 'sna' : bm) : 'sna';
    entry = d.entryPoint || s.entry_point || '';
    base = d.base && d.base !== 'none' ? d.base : '';
    isInclude = !!s.is_include; incFilename = s.filename || '';
    // Profil/lien/conteneur : DB seule, jamais dans la directive ;z80: (docs/adr/0001-...).
    profile = s.profile || ''; ldFilename = s.ld_filename || ''; container = s.container || 'sna';
    if (line != null) { await tick(); editor.gotoLine(line); }
    if (doRun) await run(); // charge -> assemble -> envoie à l'émulateur en un clic
  }

  // ---- Navigation depuis le journal (l'erreur est souvent dans un fichier inclus) ----

  // Clic sur une ligne du journal. Même fichier : simple saut. Autre fichier : ouverture —
  // précédée d'une confirmation si le tampon courant a des modifications non enregistrées,
  // parce que changer de source remplace le tampon.
  async function gotoRef(ref) {
    if (!ref) return;
    if (!ref.srcId || ref.srcId === selected?.id) { editor.gotoLine(ref.line); return; }
    await navigate(ref, { back: false });
  }

  async function navigate(ref, { back = false } = {}) {
    if (dirty) { pendingNav = { ref, back }; return; }
    pendingNav = null;
    if (back) history = history.slice(0, -1);
    await selectSource(ref.srcId, { run: false, line: ref.line, push: !back });
  }

  // Retour au fichier précédent, à la ligne d'où on était parti. Dépile : re-empiler ferait
  // de la flèche une bascule entre deux fichiers au lieu d'un historique.
  const goBack = () => {
    const prev = history[history.length - 1];
    if (prev) navigate({ srcId: prev.id, name: prev.name, line: prev.line }, { back: true });
  };

  // Réponses de la popup de confirmation.
  async function navSave() {
    const p = pendingNav; pendingNav = null;
    try { await save(); } catch (e) { log('⚠ save failed: ' + (e?.message || e), 'fail'); return; }
    if (p) { dirty = false; await navigate(p.ref, { back: p.back }); }
  }
  async function navDiscard() {
    const p = pendingNav; pendingNav = null;
    dirty = false; // l'abandon est explicite : sans ça, navigate() redemanderait
    if (p) await navigate(p.ref, { back: p.back });
  }

  function newSource() {
    selected = { name: 'untitled', author: null, description: null };
    setCode('; z80: assembler=fantams buildmode=sna entry=#8000\n  org #8000\nstart:\n  ret\n');
    dirty = false;
    asm = 'fantams'; buildmode = 'sna'; entry = '#8000'; base = '';
    isInclude = false; incFilename = '';
    profile = ''; ldFilename = ''; container = 'sna';
  }

  // Redimensionnement du journal (quand il est sous l'éditeur) : on suit le pointeur.
  function startResize(e) {
    e.preventDefault();
    const y0 = e.clientY, h0 = logH;
    const move = (ev) => { logH = Math.max(60, Math.min(window.innerHeight - 200, h0 + (y0 - ev.clientY))); };
    const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); };
    window.addEventListener('pointermove', move); window.addEventListener('pointerup', up);
  }

  async function run() {
    savedListScroll = listEl ? listEl.scrollTop : 0; // à préserver malgré le lancement de l'émulateur
    busy = true; logLines = []; log('Assembling…');
    mainId = selected?.id ?? null; mainName = selected?.name || 'current source';
    const cfg = { assembler: asm || undefined, buildmode, entryPoint: entry || undefined,
                  base: base || undefined,
                  // Profil/lien/conteneur : jamais dans la directive ;z80: (docs/adr/0001-...) —
                  // upsertDirectives les ignore (hors de DIR_KEYS), c'est volontaire.
                  profile: (!isInclude && profile) || undefined,
                  ldFile: (!isInclude && ldFilename) || undefined,
                  container: (!isInclude && container) || undefined,
                  cprBank: (!isInclude && container === 'cpr') ? cprBank : undefined };
    const up = upsertDirectives(editor.value, cfg);  // maintien de la ligne ;z80:
    if (up !== editor.value) setCode(up);            // n'écrit que si ça change (évite la boucle auto)
    try {
      // Injecte toutes les sources marquées "librairie" (is_include) dans le FS wasm, sauf la source
      // en cours (déjà écrite comme /in.asm — s'auto-inclure ne servirait à rien).
      try { includeCache = await store.listIncludes(); } catch { includeCache = includeCache || []; }
      const includes = (includeCache || []).filter((i) => i.id !== selected?.id);
      const t0 = performance.now();
      const res = await assemble({ code: editor.value, ...cfg, includes, resolveBase }, factories);
      const dt = (performance.now() - t0).toFixed(0);
      preText = res.preprocessed || ''; // dispo même en cas d'échec (pour debug)
      lineOffset = res.lineOffset || 0;
      const built = res.ok && !!res.output;
      applyStatus(built); // met à jour l'indicateur (liste + source courante, + base en mode complet)
      for (const l of res.log.slice(built ? -12 : -400)) log(l, severity(l), resolveRef(l)); // en cas d'échec, tout garder
      if (res.error) log('⚠ ' + res.error, 'err', resolveRef(res.error));
      if (!built) {
        log(`❌ Failed (${res.assembler}) — ${dt} ms`, 'fail');
        emuUrl = ''; // l'émulateur affichait le binaire précédent : on libère la colonne pour le journal
        busy = false; return;
      }
      log(`✔ ${res.assembler} → .${res.ext} ${res.output.length} bytes in ${dt} ms`, 'ok');
      if (dlUrl) URL.revokeObjectURL(dlUrl);
      dlExt = res.ext || 'sna';
      dlUrl = URL.createObjectURL(new Blob([res.output], { type: 'application/octet-stream' }));
      if (amspiritEnabled && amspiritConnected && (res.ext === 'sna' || res.ext === 'cpr')) {
        try {
          const name = (selected?.name || 'z80next').replace(/[^\w.-]+/g, '_') + '.' + res.ext;
          await injectMedia(amspiritUrl, res.output, { name, onLog: (m) => log('AMSpiriT: ' + m, 'muted') });
          log('✔ injected into AMSpiriT', 'ok');
        } catch (e) {
          amspiritConnected = false; // la sonde périodique retentera la connexion
          log('⚠ AMSpiriT injection failed: ' + (e?.message || e), 'fail');
        }
      } else if (isFantams && profile === 'cpcplus') {
        // tiny8bit (l'émulateur intégré) ne gère pas le CPC+ ; seul AMSpiriT le fait pour
        // l'instant (Q7/Q5 du design). Pas de tentative vouée à l'échec : juste le journal
        // et le téléchargement, déjà disponibles ci-dessus.
        emuUrl = '';
        log('cpcplus needs AMSpiriT (the built-in emulator has no CPC+ support) — enable it in Settings, or use the download.', 'fail');
      } else {
        const url = await feedEmulator(res.output, res.ext);
        if (url) emuUrl = `/emu/tiny8bit/cpc.html?file=${encodeURIComponent(url)}`;
        else log('Service worker unavailable — use the download instead.', 'fail');
      }
    } catch (e) { log('Error: ' + (e?.message || e), 'fail'); emuUrl = ''; }
    busy = false;
    await tick(); restoreListScroll(); // rétablit la position des vignettes après le rendu du panneau/iframe
  }

  async function save() {
    const data = { name: selected?.name || 'untitled', code: editor.value, assembler: asm || null,
      buildmode, entry_point: entry || null, author: selected?.author || null, description: selected?.description || null,
      is_include: isInclude ? 1 : null, filename: isInclude ? (incFilename || null) : null,
      profile: isInclude ? null : (profile || null),
      ld_filename: isInclude ? null : (ldFilename || null),
      container: isInclude ? null : (container || null) };
    const saved = selected?.id ? await store.update(selected.id, data) : await store.create(data);
    selected = saved; dirty = false; await refreshList(); log('💾 saved: ' + saved.name, 'ok');
  }
  async function fork() {
    if (!selected?.id) return;
    const f = await store.fork(selected.id, {}); selected = f; dirty = false; await refreshList(); log('⑂ forked: ' + f.name, 'ok');
  }

  // Met à jour l'indicateur d'assemblage (✅/❌) après un build, sans attendre un re-classify.
  // Une librairie (is_include) n'a pas de point d'entrée : l'assembler seule échoue normalement,
  // ce n'est pas un statut à retenir.
  function applyStatus(ok) {
    if (isInclude) return;
    const status = ok ? 'ok' : 'fail';
    const prev = selected?.build_status;
    if (selected) selected.build_status = status;
    const it = selected?.id ? sources.find((s) => s.id === selected.id) : null;
    if (it) { it.build_status = status; it.compilable = ok ? 1 : 0; }
    // Persistance en base uniquement en mode complet, pour une source enregistrée, si le statut change.
    if (canWrite && selected?.id && prev !== status) {
      store.update(selected.id, { build_status: status, compilable: ok ? 1 : 0 }).catch(() => {});
    }
  }

  // Mise en forme du tampon par fantams (ADR 0013) : deux-points sur les labels
  // qui en manquent, quatre espaces devant les lignes de code. Rien d'autre.
  //
  // Passe par `editor.value` et non `setCode` : le tampon a bel et bien changé,
  // donc `dirty` doit le dire. L'annulation est celle de l'éditeur (Ctrl+Z), qui
  // voit ça comme une modification de plus.
  async function beautify() {
    if (!editor || busy) return;
    const before = editor.value;
    busy = true;
    try {
      // Les mêmes includes que pour l'assemblage : la mise en forme a besoin de
      // leurs macros pour ne pas prendre un appel nu pour un label. Le cache n'est
      // rempli que par run() — sans ce repli, mettre en forme avant d'avoir
      // assemblé une fois se ferait à l'aveugle sur les bibliothèques.
      if (!includeCache) {
        try { includeCache = await store.listIncludes(); } catch { includeCache = []; }
      }
      const incs = (includeCache || []).filter((i) => i.id !== selected?.id);
      const r = await beautifySource(before, factories, incs);
      if (!r.ok || r.code === null) {
        log('⚠ Formatting failed: ' + (r.error || r.log.slice(-1)[0] || 'fantams error'), 'fail');
        return;
      }
      if (r.code === before) { log('Already formatted.', 'muted'); return; }
      // La règle 3 du beautify détache « label: instruction » en deux lignes
      // (ADR 0017), donc la mise en forme n'est plus bijective sur les lignes :
      // le curseur descend d'autant de lignes qu'il y a de labels collés AU-DESSUS
      // de la sienne. Rien d'autre n'en dépend — l'assembleur travaille sur la
      // provenance des lignes préprocessées, pas sur ce texte.
      const line = editor.cursorLine;
      editor.value = r.code;
      editor.gotoLine(line + countDetachedBefore(before, line));
      log('Formatted.', 'ok');
    } catch (e) {
      log('⚠ Formatting failed: ' + (e?.message || e), 'fail');
    } finally {
      busy = false;
    }
  }

  // Lignes que le détachement va couper, strictement avant `line1` (1-indexée).
  // Miroir de la règle 3 : un identifiant, son deux-points, puis du code — le
  // deux-points est ce qui lève le doute avec un appel de macro (« sprite 4,12 »).
  const GLUED_LABEL = /^\s*[A-Za-z_.@][\w.@]*\s*:\s*\S/;
  function countDetachedBefore(text, line1) {
    const lines = text.split('\n');
    let n = 0;
    for (let i = 0; i < Math.min(line1 - 1, lines.length); i++) {
      const code = lines[i].split(';')[0];
      if (GLUED_LABEL.test(code)) n++;
    }
    return n;
  }

  async function openPre() {
    if (!preText) return;
    showPre = true;
    await tick(); // attend le rendu de la modale avant de monter le viewer
    if (preEl) { preViewer?.destroy(); preViewer = makeViewer(preEl, preText); }
  }
  function closePre() { showPre = false; preViewer?.destroy(); preViewer = null; }
  async function copyPre() { try { await navigator.clipboard.writeText(preText); log('Copied.', 'ok'); } catch { log('Copy rejected by the browser.', 'fail'); } }

  onMount(async () => {
    // --- DIAGNOSTIC TEMPORAIRE : trace la cause d'un rechargement de page ---
    window.addEventListener('beforeunload', () => { console.trace('⚠️ [diag] beforeunload — la page va se recharger/quitter'); });
    window.addEventListener('pagehide', (e) => { console.log('⚠️ [diag] pagehide persisted=', e.persisted); });
    navigator.serviceWorker?.addEventListener?.('controllerchange', () => { console.log('⚠️ [diag] SW controllerchange'); });
    console.log('✅ [diag] page chargée à', new Date().toISOString());
    // --- fin diagnostic ---
    editor = makeEditor(editorEl, '', onEdit);
    await initSW();
    loadSettings();
    syncAmspiritPolling();
    store = await openStore({ base: '', loadLocalDb });
    mode = store.mode; count = await store.count();
    await refreshList();
    log(`Store: ${mode} mode — ${count} sources.`, 'muted');
    // Sans action de l'utilisateur : la version doit etre la AVANT qu'on en ait
    // besoin, sinon elle ne sert a rien le jour ou on en a besoin.
    fantamsVersion(factories)
      .then((v) => { fantamsVer = v.ok ? v.version : 'fantams: version inconnue'; })
      .catch(() => { fantamsVer = 'fantams: version inconnue'; });
    return () => { editor?.destroy(); clearInterval(amspiritTimer); };
  });

  const badge = (s) => s.is_include ? '🧩' : s.build_status === 'ok' ? '✅' : s.build_status === 'external-dep' ? '📦' : '·';
  const fmtDate = (ms) => ms ? new Date(Number(ms)).toISOString().slice(0, 10) : '';

  // Icônes de la barre d'outils : traits (pas d'emoji, cohérent quel que soit l'OS/navigateur).
  const ICON_PATHS = {
    settings: '<circle cx="10" cy="10" r="2.6"/><path d="M10 2.6v2M10 15.4v2M17.4 10h-2M4.6 10h-2M15.2 4.8l-1.4 1.4M6.2 13.8l-1.4 1.4M15.2 15.2l-1.4-1.4M6.2 6.2 4.8 4.8"/>',
    plus: '<path d="M10 4v12M4 10h12"/>',
    edit: '<path d="M12.6 3.4 16.6 7.4 6.6 17.4H2.6v-4Z"/>',
    play: '<path d="M6 3.6v12.8L16 10Z"/>',
    refresh: '<path d="M16 6.5A6.4 6.4 0 0 0 4.2 8M4 13.5A6.4 6.4 0 0 0 15.8 12"/><path d="M16 3v3.5H12.5M4 17v-3.5H7.5"/>',
    save: '<path d="M4 3h9.5L16 5.5V17H4Z"/><path d="M6.5 3v4.5h6V3M6.5 17v-5.5h7V17"/>',
    fork: '<circle cx="6" cy="5" r="1.8"/><circle cx="14" cy="5" r="1.8"/><circle cx="10" cy="15" r="1.8"/><path d="M6 6.8v2.2c0 1.2 1 2 2.2 2h3.6c1.2 0 2.2-.8 2.2-2V6.8M10 11v2.2"/>',
    terminal: '<rect x="2.5" y="3.5" width="15" height="13" rx="1.4"/><path d="M5.5 8l2.6 2.2-2.6 2.2M10.5 12.6h4"/>',
    align: '<path d="M3 4.5h14M3 8.5h9M3 12.5h14M3 16.5h9"/>',
    layers: '<path d="M10 3 3 6.6 10 10.2 17 6.6Z"/><path d="M3 10.4 10 14l7-3.6M3 13.9 10 17.5l7-3.6"/>',
    download: '<path d="M10 3v9.5M6 9l4 4 4-4M3.5 16.5h13"/>',
    check: '<path d="M4 10.5l4 4 8-9"/>',
    alert: '<path d="M10 3 17.5 16.5h-15Z"/><path d="M10 8v3.4M10 14v.1"/>',
    x: '<path d="M5 5l10 10M15 5 5 15"/>',
    sidebar: '<rect x="2.5" y="4" width="15" height="12" rx="1.5"/><path d="M8 4v12"/>',
  };
  // Icône associée à une ligne du journal (null = pas d'icône, ex. lignes neutres).
  const logIcon = (cls) => cls === 'err' || cls === 'fail' ? 'x' : cls === 'warn' ? 'alert' : cls === 'ok' ? 'check' : null;
</script>

<svelte:window onkeydown={(e) => {
  if (e.key === 'Escape') { if (showPre) closePre(); else if (showSettings) applySettings(); return; }
  if ((e.ctrlKey || e.metaKey) && !e.altKey && (e.key === 'r' || e.key === 'R')) {
    e.preventDefault(); // pas de rechargement de page : on réassemble à la place
    if (!busy) run();
    return;
  }
  if ((e.ctrlKey || e.metaKey) && !e.altKey && (e.key === 's' || e.key === 'S')) {
    e.preventDefault(); // pas d'enregistrement de la page : on sauvegarde la source à la place
    if (canWrite && !busy) save();
    return;
  }
  // Alt+Maj+F : le raccourci « mettre en forme » qu'on a partout ailleurs.
  if (e.altKey && e.shiftKey && (e.key === 'F' || e.key === 'f')) {
    e.preventDefault();
    if (!busy) beautify();
  }
}} />

{#snippet icon(name, size = 16)}
  <svg class="ic" width={size} height={size} viewBox="0 0 20 20" aria-hidden="true">{@html ICON_PATHS[name] || ''}</svg>
{/snippet}

<header>
  <button class="ico" class:active={showList} onclick={() => showList = !showList}
    title={showList ? 'Hide the source list' : 'Show the source list'}>{@render icon('sidebar')}</button>
  <strong>z80live</strong>
  {#if fantamsVer}
    <span class="ver" title="Version date (what the maintainer meant to ship) and build date of this very artifact. A gap between them means the artifact is behind the sources.">{fantamsVer}</span>
  {/if}
  <span class="mode" class:lite={mode === 'local'}
    title={mode === 'local' ? 'Offline mode: static catalog, no saving possible' : mode === 'api' ? 'Connected to the server: saving and creating sources is possible' : ''}>
    {mode === 'local' ? '○ offline' : mode === 'api' ? '● connected' : '…'}
  </span>
  <input class="search" placeholder="Search…" bind:value={query} oninput={onSearch} />
  <label class="grp">group by
    <select bind:value={groupBy}>
      <option value="">— flat list</option>
      <option value="genre">genre</option>
      <option value="author">author</option>
      <option value="group_name">group</option>
      <option value="buildmode">output type</option>
      <option value="assembler">assembler</option>
    </select>
  </label>
  <span class="grow"></span>
  {#if amspiritEnabled}
    <span class="amsp" class:on={amspiritConnected} title={amspiritConnected ? 'AMSpiriT connected: ' + amspiritUrl : 'AMSpiriT enabled but unreachable — falling back to the built-in wasm emulator'}>
      ● AMSpiriT
    </span>
  {/if}
  {#if canWrite}<button class="ico" onclick={openProjects} title="CPR projects: build a multi-bank cartridge from several sources">CPR</button>{/if}
  <button class="ico" onclick={openAppSettings} title="Application settings">{@render icon('settings')}</button>
  {#if canWrite}<button class="ico" onclick={newSource} title="New source">{@render icon('plus', 14)} New</button>{/if}
</header>

<!-- Le journal vit à deux endroits (sous l'éditeur, ou dans la colonne de l'émulateur) :
     un seul snippet, pour que le compte et le filtre soient les mêmes des deux côtés. -->
{#snippet logPane(style)}
  <div class="logwrap" style={style}>
    {#if errCount || warnCount}
      <div class="logbar">
        {#if errCount}<span class="err">{@render icon('x', 12)} {errCount} error{errCount > 1 ? 's' : ''}</span>{/if}
        {#if warnCount}
          <span class="warn">{@render icon('alert', 12)} {warnCount} warning{warnCount > 1 ? 's' : ''}</span>
          <button class="flt" class:off={!showWarnings} onclick={() => showWarnings = !showWarnings}
            title="Hide warnings to keep only errors">{showWarnings ? 'hide' : 'show'}</button>
        {/if}
      </div>
    {/if}
    <div class="log">
      {#each shownLines as l}
        <div class={l.cls} class:goto={!!l.ref} class:other={!!l.ref && l.ref.srcId && l.ref.srcId !== selected?.id}
          title={l.ref ? (l.ref.srcId && l.ref.srcId !== selected?.id ? `Open “${l.ref.name}” line ${l.ref.line}` : `Go to line ${l.ref.line}`) : null}
          role={l.ref ? 'button' : undefined} tabindex={l.ref ? 0 : undefined}
          onclick={() => gotoRef(l.ref)}
          onkeydown={l.ref ? (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); gotoRef(l.ref); } } : undefined}>
          <span class="li">{#if logIcon(l.cls)}{@render icon(logIcon(l.cls), 12)}{/if}</span><span class="lm">{l.m}</span>
        </div>
      {/each}
    </div>
  </div>
{/snippet}

<main class:no-list={!showList} class:no-emu={amspiritEnabled && amspiritConnected}>
  {#if showList}
  <aside class="list" bind:this={listEl}>
    {#each groups as g (g.key)}
      {#if g.key !== null}
        <button class="ghead" onclick={() => toggle(g.key)}>
          <span class="caret">{collapsed[g.key] ? '▸' : '▾'}</span>
          <span class="gname">{g.key}</span>
          <span class="cnt">{g.items.length}</span>
        </button>
      {/if}
      {#if g.key === null || !collapsed[g.key]}
        {#each g.items as s (s.id)}
          <button class="item" class:sel={selected?.id === s.id} class:nested={g.key !== null} onclick={() => selectSource(s.id)}>
            <span class="bdg">{badge(s)}</span>
            <span class="nm">{s.name}</span>
            {#if groupBy !== 'author' && s.author}<span class="au">{s.author}</span>{/if}
          </button>
        {/each}
      {/if}
    {:else}
      <div class="empty">No sources.</div>
    {/each}
  </aside>
  {/if}

  <section class="mid">
    <div class="controls">
      <div class="tgrp">
        <button class="ico" onclick={openSettings} disabled={!selected} title="Source settings">{@render icon('edit')}</button>
        <button class="ico primary" onclick={run} disabled={busy} title="Assemble &amp; run (Ctrl+R)">{@render icon('play')} Run</button>
        <label class="auto" title="Automatically reassemble on every change (500 ms)">
          {@render icon('refresh', 14)} <input type="checkbox" bind:checked={auto} onchange={onAutoToggle} /> auto
        </label>
      </div>
      {#if canWrite}
      <div class="tgrp">
        <button class="ico" class:dirty onclick={save} title={dirty ? 'Save (unsaved changes)' : 'Save'}>{@render icon('save')}{#if dirty} Save{/if}</button>
        {#if selected?.id}<button class="ico" onclick={fork} title="Fork">{@render icon('fork')}</button>{/if}
      </div>
      {/if}
      <div class="tgrp">
        <button class="ico" class:primary={logBig} onclick={() => logBig = !logBig} title="Expand the assembly log (right column)">{@render icon('terminal')}</button>
        <button class="ico" onclick={beautify} disabled={busy} title="Format (Alt+Shift+F)">{@render icon('align')}</button>
        {#if preText}<button class="ico" onclick={openPre} title="Code after the preprocessor">{@render icon('layers')}</button>{/if}
      </div>
      {#if dlUrl}<a class="ico dl" href={dlUrl} download={dlName} title="Download the .{dlExt}">{@render icon('download')} .{dlExt}</a>{/if}
    </div>

    {#if selected}
      <div class="meta">
        <span class="st" class:ok={selected.build_status === 'ok'} class:ko={selected.build_status === 'fail'}
          title="Status of this source's last assembly — detail and settings in ✏️">{isInclude ? 'library (included by other sources)' : selected.build_status === 'ok' ? '✓ build OK' : selected.build_status === 'fail' ? '✗ build failed' : selected.build_status === 'external-dep' ? 'external dep.' : 'not tested'}</span>
        {#if history.length}
          <button class="back" onclick={goBack}
            title={`Back to “${history[history.length - 1].name}” (line ${history[history.length - 1].line})`}>←</button>
        {/if}
        <span class="t">{selected.name}</span>
        <span class="by">{selected.author ? 'by ' + selected.author : 'unknown author'}</span>
        {#if selected.owner && selected.owner !== selected.author}<span class="tags">(owner: {selected.owner})</span>{/if}
        {#if selected.description}<div class="d">{selected.description}</div>{/if}
      </div>
    {/if}

    <div class="editor" bind:this={editorEl}></div>

    {#if !logAside}
      <div class="split" role="separator" aria-orientation="horizontal" tabindex="-1" onpointerdown={startResize} title="Drag to resize the log"></div>
      {@render logPane(`flex: 0 0 ${logH}px`)}
    {/if}
  </section>

  {#if emuCol}
  <section class="emu">
    {#if logAside}
      {@render logPane('flex: 1; min-height: 0')}
      {#if emuUrl}<button class="ico" onclick={() => logBig = false}>↩ back to emulator</button>{/if}
    {/if}
    <!-- l'iframe reste montée (masquée) : la démonter relancerait l'émulateur à chaque bascule -->
    {#if emuUrl}<iframe class:hidden={logAside} title="emulator" src={emuUrl} allow="autoplay; gamepad" onload={restoreListScroll}></iframe>
    {:else if !logAside}<div class="ph">The emulator will appear here after assembly.</div>{/if}
  </section>
  {/if}
</main>

<!-- Changer de source remplace le tampon de l'éditeur : quand il porte des modifications non
     enregistrées, la navigation s'arrête ici plutôt que de les perdre en silence. -->
{#if pendingNav}
<div class="modal" onclick={() => pendingNav = null} role="presentation">
  <div class="dialog small" onclick={(e) => e.stopPropagation()} role="dialog" aria-modal="true" tabindex="-1">
    <div class="dhead"><span>Unsaved changes</span></div>
    <div class="confirm">
      <p>
        “{selected?.name || 'the current source'}” has unsaved changes.
        Opening “{pendingNav.ref.name}”{#if pendingNav.ref.line} line {pendingNav.ref.line}{/if} will lose them.
      </p>
      <div class="actions">
        <button onclick={() => pendingNav = null}>Cancel</button>
        <span class="grow"></span>
        <button onclick={navDiscard}>Open without saving</button>
        {#if canWrite}<button class="primary" onclick={navSave}>Save and open</button>{/if}
      </div>
    </div>
  </div>
</div>
{/if}

{#if showSettings && selected}
<div class="modal" onclick={applySettings} role="presentation">
  <div class="dialog small" onclick={(e) => e.stopPropagation()} role="dialog" aria-modal="true" tabindex="-1">
    <div class="dhead"><span>Source settings</span><span class="grow"></span><button onclick={applySettings}>OK ✕</button></div>
    <div class="form">
      <label>Name <input bind:value={selected.name} disabled={!canWrite} /></label>
      <label>Author <input bind:value={selected.author} disabled={!canWrite} /></label>
      <label class="wide">Description <input bind:value={selected.description} disabled={!canWrite} /></label>
      <label>Assembler
        <select bind:value={asm}><option value="">auto (rasm)</option><option>rasm</option><option value="fantams">fantams</option><option>sjasmplus</option></select>
      </label>
      <label>Output type
        <select value={isInclude ? 'lib' : buildmode} onchange={(e) => {
          const v = e.target.value;
          if (v === 'lib') { isInclude = true; } else { isInclude = false; buildmode = v; }
        }}>
          <option value="sna">sna</option>
          <option value="sna_cpc6128">sna_cpc6128</option>
          <option value="sna_cpc464">sna_cpc464</option>
          <option value="lib">🧩 library / include file (no entry point)</option>
        </select>
      </label>
      <label title="Post-boot snapshot the assembly is laid on top of: without it, firmware indirection vectors are zero and any CALL &amp;BBxx jumps into nowhere.">Base (firmware)
        <select bind:value={base}>
          <option value="">none (zeroed memory)</option>
          {#each bases as b}<option value={b.id}>{b.label || b.id}</option>{/each}
        </select>
      </label>
      {#if isInclude}
        <label class="wide">Filename (for include/incbin from another file)
          <input bind:value={incFilename} placeholder={(selected.name || 'lib').replace(/[^\w.-]+/g, '_') + '.asm'} />
        </label>
      {:else}
        <label>Entry point <input bind:value={entry} placeholder="#8000" /></label>
        {#if isFantams}
          <label title="fantams --target: the CPC variant this source targets. Empty = fantams's own default.">Profile
            <select bind:value={profile}>
              <option value="">auto (fantams default)</option>
              <option value="cpc6128">cpc6128</option>
              <option value="cpcplus">cpcplus</option>
            </select>
          </label>
          <label title="What fantams builds (-o extension). sna and cpr are implemented; dsk/cdt are fantams's next evolution.">Container
            <select bind:value={container}>
              <option value="sna">sna</option>
              <option value="dsk" disabled>dsk (coming soon)</option>
              <option value="cdt" disabled>cdt (coming soon)</option>
              <option value="cpr">cpr</option>
            </select>
          </label>
          <label class="wide" title="fantams -T: an included .ld file (a source flagged as library, named *.ld) that places this build's sections. Optional — leave empty to write org/placement by hand.">Linker script (.ld)
            <select bind:value={ldFilename}>
              <option value="">none</option>
              {#each ldOptions as o}<option value={o.filename}>{o.filename}</option>{/each}
            </select>
          </label>
          {#if container === 'cpr'}
            <label title="fantams --cpr-bank: the physical cartridge ROM id (0..31) THIS source's build fills. This tests one bank in isolation — a full multi-bank cartridge is built from the CPR projects panel.">Physical bank (0..31)
              <input type="number" min="0" max="31" bind:value={cprBank} />
            </label>
          {/if}
        {/if}
      {/if}
      <label>Genre
        <select bind:value={selected.genre} disabled={!canWrite}>
          <option value={null}>(unclassified)</option>
          {#each GENRES as gg}<option value={gg}>{gg}</option>{/each}
          {#if selected.genre && !GENRES.includes(selected.genre)}<option value={selected.genre}>{selected.genre}</option>{/if}
        </select>
      </label>
      {#if !canWrite}<div class="note wide">Read-only mode: the assembler / type / entry point apply to this session only; the name, author, description and genre are not saved.</div>{/if}
    </div>
  </div>
</div>
{/if}

{#if showAppSettings}
<div class="modal" onclick={closeAppSettings} role="presentation">
  <div class="dialog small" onclick={(e) => e.stopPropagation()} role="dialog" aria-modal="true" tabindex="-1">
    <div class="dhead"><span>Settings</span><span class="grow"></span><button onclick={closeAppSettings}>OK ✕</button></div>
    <div class="form">
      <label class="wide chk">
        <input type="checkbox" bind:checked={amspiritEnabled} onchange={syncAmspiritPolling} />
        Drive AMSpiriT (external emulator) instead of the built-in wasm one
      </label>
      <label class="wide">AMSpiriT server URL
        <input bind:value={amspiritUrl} placeholder="http://127.0.0.1:8765" disabled={!amspiritEnabled} onchange={syncAmspiritPolling} />
      </label>
      <div class="wide note">
        {#if !amspiritEnabled}Built-in wasm emulator (default).
        {:else if amspiritConnected}✅ connected — the wasm panel is hidden, execution goes through RAM + PC injection via the AMSpiriT API.
        {:else}⏳ unreachable for now — falling back to the built-in wasm emulator until the connection is established.{/if}
      </div>
    </div>
  </div>
</div>
{/if}

{#if showProjects}
<div class="modal" onclick={closeProjects} role="presentation">
  <div class="dialog" onclick={(e) => e.stopPropagation()} role="dialog" aria-modal="true" tabindex="-1">
    <div class="dhead"><span>CPR projects</span><span class="grow"></span><button onclick={closeProjects}>Close ✕</button></div>
    <div class="form">
      <div class="note wide">
        A CPR cartridge is built one physical ROM bank (0..31) at a time (fantams/cpr.h : each
        bank is linked separately). A project is just the ordered list of which source fills
        which bank — each source still needs its own profile/container=cpr/linker script set
        in its own settings.
      </div>
      <label class="wide">Project
        <select value={currentProject?.id || ''} onchange={(e) => e.target.value ? openProject(e.target.value) : (currentProject = null)}>
          <option value="">— choose —</option>
          {#each projects as p}<option value={p.id}>{p.name} ({p.bank_count} bank{p.bank_count === 1 ? '' : 's'})</option>{/each}
        </select>
      </label>
      <button onclick={createProjectPrompt}>+ New project</button>
      {#if currentProject}
        <label class="wide">Name
          <input value={currentProject.name}
                 onchange={(e) => renameCurrentProject(e.target.value)} />
        </label>
        <table class="wide banks">
          <thead><tr><th>bank</th><th>source</th><th></th></tr></thead>
          <tbody>
            {#each [...currentProject.banks].sort((a, b) => a.bank - b.bank) as b}
              <tr>
                <td>{b.bank}</td>
                <td>{b.source_name}</td>
                <td><button onclick={() => removeBank(b.bank)} title="Remove this bank from the project">✕</button></td>
              </tr>
            {/each}
          </tbody>
        </table>
        <label title="Assigns the source currently open in the editor to this bank number.">Add current source as bank
          <input type="number" min="0" max="31" bind:value={newBankNumber} />
        </label>
        <button disabled={!selected?.id} onclick={addCurrentAsBank}>
          + add “{selected?.name || '(no source open)'}” as bank {newBankNumber}
        </button>
        <button class="primary wide" disabled={projectBusy || currentProject.banks.length === 0} onclick={buildProject}>
          {projectBusy ? 'Building…' : `Build ${currentProject.name}.cpr (${currentProject.banks.length} bank${currentProject.banks.length === 1 ? '' : 's'})`}
        </button>
        {#if projectDlUrl}
          <a class="wide" href={projectDlUrl} download={(currentProject.name || 'project').replace(/[^\w.-]+/g, '_') + '.cpr'}>⬇ download .cpr</a>
        {/if}
        {#if projectLog.length}
          <div class="wide log" style="max-height:160px">
            {#each projectLog as l}<div>{l}</div>{/each}
          </div>
        {/if}
        <button class="wide" onclick={() => deleteProject(currentProject.id)}>Delete this project</button>
      {/if}
    </div>
  </div>
</div>
{/if}

{#if showPre}
<div class="modal" onclick={closePre} role="presentation">
  <div class="dialog" onclick={(e) => e.stopPropagation()} role="dialog" aria-modal="true" tabindex="-1">
    <div class="dhead">
      <span>Code sent to the assembler (after the preprocessor)</span>
      <span class="grow"></span>
      <button onclick={copyPre}>Copy</button>
      <button onclick={closePre}>Close ✕</button>
    </div>
    <div class="dbody" bind:this={preEl}></div>
  </div>
</div>
{/if}

<style>
  :global(html, body, #app) { height: 100%; margin: 0; overflow: hidden; }
  :global(body) { background: #181a1b; color: #e3e3e3; font: 14px/1.4 system-ui, sans-serif; }
  header { display: flex; align-items: center; gap: .6rem; padding: .5rem .8rem; background: #111; border-bottom: 1px solid #333; }
  header strong { color: #7cf; }
  .mode { font-size: 11px; padding: .1rem .4rem; border: 1px solid #375; color: #7d9; border-radius: 4px; }
  .ver { font-size: 11px; color: #889; font-family: ui-monospace, monospace; white-space: nowrap; }
  .mode.lite { border-color: #a83; color: #db8; }
  .search { flex: 0 1 260px; padding: .3rem .5rem; background: #22262a; color: #eee; border: 1px solid #444; border-radius: 5px; }
  .grow { flex: 1; }
  button, select, input, a.dl { font: inherit; background: #22262a; color: #eee; border: 1px solid #444; border-radius: 5px; padding: .3rem .5rem; cursor: pointer; text-decoration: none; }
  button.primary { background: #2d6; color: #052; border-color: #2d6; font-weight: 700; }
  button:disabled { opacity: .5; cursor: wait; }
  main { display: grid; grid-template-columns: 240px 1fr 1fr; gap: .5rem; padding: .5rem; height: calc(100vh - 49px); box-sizing: border-box; overflow: hidden; }
  main.no-list { grid-template-columns: 1fr 1fr; }
  main.no-emu { grid-template-columns: 240px 1fr; }
  main.no-list.no-emu { grid-template-columns: 1fr; }
  .amsp { font-size: 11px; padding: .1rem .5rem; border: 1px solid #833; color: #e88; border-radius: 4px; }
  .amsp.on { border-color: #375; color: #7d9; }
  /* Les cellules doivent pouvoir rétrécir sous leur contenu (sinon débordement -> scroll de page). */
  .list, .mid, .emu { min-width: 0; min-height: 0; }
  .list { overflow: auto; border: 1px solid #333; border-radius: 6px; background: #0d0f10; }
  .ghead { position: sticky; top: 0; z-index: 1; display: flex; gap: .35rem; align-items: center; width: 100%; text-align: left; padding: .3rem .5rem; background: #171a1d; border: none; border-bottom: 1px solid #2a2f34; border-radius: 0; color: #9cf; font-weight: 600; }
  .ghead .caret { width: .8em; color: #678; }
  .ghead .gname { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .ghead .cnt { font-size: 11px; color: #789; background: #0d0f10; border-radius: 8px; padding: 0 .4rem; }
  .item { display: flex; gap: .4rem; align-items: baseline; width: 100%; border: none; border-bottom: 1px solid #1c1f22; border-radius: 0; text-align: left; padding: .35rem .5rem; background: none; }
  .item.nested { padding-left: 1.2rem; }
  .grp { font-size: 12px; color: #9aa; display: flex; gap: .3rem; align-items: center; }
  .meta .genre select { font: inherit; font-size: 11px; background: #22262a; color: #cfe3ff; border: 1px solid #444; border-radius: 4px; }
  .meta .ro { color: #a76; }
  .item:hover { background: #ffffff08; }
  .item.sel { background: #1f3a52; }
  .item .bdg { font-size: 11px; }
  .item .nm { flex: 1; color: #cfe3ff; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .item .au { font-size: 11px; color: #789; }
  .empty { padding: 1rem; color: #789; }
  .mid, .emu { display: flex; flex-direction: column; gap: .4rem; }
  .controls { display: flex; gap: .5rem; align-items: center; flex-wrap: wrap; }
  .controls label { font-size: 12px; color: #9aa; display: flex; gap: .3rem; align-items: center; }
  .ic { flex: none; stroke: currentColor; fill: none; stroke-width: 1.6; stroke-linecap: round; stroke-linejoin: round; }
  .tgrp { display: flex; align-items: center; gap: .25rem; background: #14171a; border: 1px solid #3a3f44; border-radius: 8px; padding: .25rem; }
  .ico { display: flex; align-items: center; gap: .4rem; padding: .3rem .5rem; font-size: 13px; line-height: 1; }
  .tgrp .ico { border: none; background: none; border-radius: 5px; }
  .ico.primary { background: #2d6; color: #052; border-color: #2d6; font-weight: 700; }
  .ico.dirty { background: #c33; color: #fff; border-color: #c33; font-weight: 700; }
  header .ico.active { color: #7cc9ff; border-color: #375; }
  .auto { font-size: 12px; color: #9aa; }
  .form { padding: .8rem; display: grid; grid-template-columns: 1fr 1fr; gap: .6rem .8rem; overflow: auto; }
  .form label { display: flex; flex-direction: column; gap: .2rem; font-size: 12px; color: #9aa; }
  .form label.wide, .form .note.wide { grid-column: 1 / -1; }
  .form label.chk { flex-direction: row; align-items: center; gap: .4rem; }
  .form input, .form select { font: inherit; background: #22262a; color: #eee; border: 1px solid #444; border-radius: 5px; padding: .35rem .5rem; }
  .form .note { font-size: 11px; color: #a76; }
  .meta { background: #0d0f10; border: 1px solid #333; border-radius: 6px; padding: .4rem .6rem; font-size: 12px; }
  .meta .st { font-size: 11px; padding: .05rem .35rem; border: 1px solid #556; border-radius: 4px; color: #9aa; margin-right: .4rem; }
  .meta .st.ok { border-color: #375; color: #7d9; }
  .meta .st.ko { border-color: #833; color: #e88; }
  .meta .t { font-weight: 700; color: #cfe3ff; }
  .meta .by { color: #7cf; margin-left: .3rem; }
  .meta .d { color: #bcd; margin-top: 2px; }
  .meta .tags { color: #89a; font-size: 11px; margin-top: 2px; }
  .editor { flex: 1; min-height: 0; overflow: hidden; border: 1px solid #333; border-radius: 6px; }
  :global(.editor .cm-editor) { height: 100%; }
  :global(.editor .cm-scroller) { overflow: auto; } /* le défilement du code reste dans l'éditeur */
  .split { flex: 0 0 6px; cursor: row-resize; border-radius: 3px; background: #2a2f34; }
  .split:hover { background: #3d6; }
  .logwrap { display: flex; flex-direction: column; min-height: 0; gap: 4px; }
  /* La barre annonce le compte AVANT le filtre : masquer les avertissements ne doit jamais
     donner l'impression qu'il n'y en avait pas. */
  .logbar { display: flex; align-items: center; gap: 10px; font-size: 11.5px; }
  .logbar .err, .logbar .warn { display: flex; align-items: center; gap: 4px; }
  .logbar .err { color: #f88; } .logbar .warn { color: #fc6; }
  .flt { background: none; border: 1px solid #444; border-radius: 4px; color: #89a; cursor: pointer; font-size: 10.5px; padding: 1px 6px; }
  .flt:hover { border-color: #666; color: #cde; }
  .flt.off { border-color: #fc6; color: #fc6; }
  .log { flex: 1; overflow: auto; margin: 0; background: #0d0f10; border: 1px solid #333; border-radius: 6px; padding: 4px 0; font-size: 11.5px; }
  .log > div { display: flex; gap: 8px; padding: 1px 8px; white-space: pre-wrap; }
  .log > div .li { flex: none; width: 12px; display: flex; justify-content: center; padding-top: 3px; }
  .log > div .lm { flex: 1; }
  .log .ok { color: #7f7; } .log .err, .log .fail { color: #f88; } .log .warn { color: #fc6; } .log .muted { color: #89a; }
  .log .err, .log .fail { background: #ff6b6b14; } .log .warn { background: #ffc26614; }
  .log .goto { cursor: pointer; }
  .log .goto .lm { text-decoration: underline dotted; text-decoration-color: currentColor; }
  .log .goto:hover { background: #ffffff12; }
  /* Une ligne qui MÈNE AILLEURS se distingue : le clic va changer le contenu de l'éditeur. */
  .log .goto.other .lm { text-decoration-style: solid; }
  .back { background: none; border: 1px solid #444; border-radius: 4px; color: #89a; cursor: pointer; font-size: 12px; line-height: 1; padding: 1px 5px; margin-right: 4px; }
  .back:hover { border-color: #7cf; color: #7cf; }
  .confirm { padding: .8rem; display: flex; flex-direction: column; gap: .8rem; font-size: 13px; }
  .confirm p { margin: 0; color: #cfe3ff; }
  .confirm .actions { display: flex; align-items: center; gap: .5rem; }
  .emu iframe.hidden { display: none; }
  .emu iframe { flex: 1; width: 100%; height: 100%; border: 1px solid #333; border-radius: 6px; background: #000; }
  .emu .ph { flex: 1; display: grid; place-items: center; color: #567; border: 1px dashed #334; border-radius: 6px; }
  .modal { position: fixed; inset: 0; background: #000a; display: grid; place-items: center; z-index: 50; }
  .dialog { width: min(900px, 92vw); height: min(80vh, 800px); display: flex; flex-direction: column; background: #16181a; border: 1px solid #444; border-radius: 8px; overflow: hidden; }
  .dialog.small { width: min(560px, 92vw); height: auto; max-height: 85vh; }
  .dhead { display: flex; align-items: center; gap: .5rem; padding: .5rem .7rem; background: #111; border-bottom: 1px solid #333; font-size: 13px; }
  .dbody { flex: 1; min-height: 0; overflow: hidden; }
  :global(.dbody .cm-editor) { height: 100%; }
  :global(.dbody .cm-scroller) { overflow: auto; }
</style>
