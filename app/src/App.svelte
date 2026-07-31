<script>
  import { onMount, tick } from 'svelte';
  import { openStore } from '../../client/store.mjs';
  import { assemble, parseDirectives, upsertDirectives } from '../../wasm/assemble.mjs';
  import { makeEditor, makeViewer } from './lib/editor.js';
  import { ping as pingAmspirit, injectSna } from './lib/amspirit.js';

  let store = $state(null);
  let mode = $state('…');
  let count = $state(0);
  let sources = $state([]);
  let query = $state('');
  let selected = $state(null);
  let asm = $state(''), buildmode = $state('sna'), entry = $state('');
  let logLines = $state([]);
  let lineOffset = 0; // nb de lignes d'en-tête injectées avant le code utilisateur par le dernier build
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
  const GENRES = ['jeu désassemblé', 'démo', 'graphisme', 'audio', 'test asm', 'tools', 'math', 'autre'];

  // Regroupement hiérarchique de la liste.
  let groupBy = $state('');            // '' | author | group_name | buildmode | assembler | genre
  let collapsed = $state({});
  const GROUP_LABEL = { author: 'auteur inconnu', group_name: '(sans groupe)', buildmode: '(type ?)', assembler: 'rasm (défaut)', genre: '(non classé)' };

  const groups = $derived.by(() => {
    if (!groupBy) return [{ key: null, items: sources }];
    const map = new Map();
    for (const s of sources) {
      const k = s[groupBy] || GROUP_LABEL[groupBy] || '(vide)';
      if (!map.has(k)) map.set(k, []);
      map.get(k).push(s);
    }
    return [...map.entries()]
      .sort((a, b) => String(a[0]).localeCompare(String(b[0]), 'fr'))
      .map(([key, items]) => ({ key, items }));
  });
  const toggle = (k) => { collapsed = { ...collapsed, [k]: !collapsed[k] }; };

  // Popup de réglages de la source (libère la barre de l'éditeur).
  let showSettings = $state(false);
  const openSettings = () => { showSettings = true; };
  async function applySettings() {
    showSettings = false;
    const it = selected?.id ? sources.find((s) => s.id === selected.id) : null;
    if (it && selected) { it.name = selected.name; it.author = selected.author; it.genre = selected.genre; it.assembler = asm || null; it.buildmode = buildmode; }
    if (canWrite && selected?.id) {
      try {
        await store.update(selected.id, {
          name: selected.name, author: selected.author, description: selected.description,
          genre: selected.genre, assembler: asm || null, buildmode, entry_point: entry || null,
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
  const factories = {
    createRasm: async (o) => (await loadWasm('rasm.mjs')).default(o),
    createSjasm: async (o) => (await loadWasm('sjasmplus.mjs')).default(o),
    createFantams: async (o) => (await loadWasm('fantams.mjs')).default(o),
  };

  // Numéro de ligne (1-indexé) dans la source *assemblée* (avec en-tête), pour les 3 formats
  // d'erreur rencontrés : rasm "[fichier:12]", sjasmplus "fichier(12):", fantams "fichier:12:".
  function parseSourceLine(text) {
    let m = /\[[^\]]*:(\d+)\]/.exec(text);
    if (!m) m = /^[^\s:()]+\((\d+)\):/.exec(text);
    if (!m) m = /^[^\s:()]+:(\d+):/.exec(text);
    return m ? parseInt(m[1], 10) : null;
  }

  // `srcLine` : numéro de ligne dans la source assemblée (avec en-tête), si applicable — recalé
  // sur la ligne de l'éditeur (sans en-tête) via lineOffset avant d'être stocké.
  const log = (m, cls = '', srcLine = null) => {
    const editorLine = srcLine != null ? srcLine - lineOffset : null;
    logLines = [...logLines, { m: m.replace(/\x1b\[[0-9;]*m/g, ''), cls, line: editorLine > 0 ? editorLine : null }];
  };

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

  async function selectSource(id) {
    const s = await store.get(id);
    selected = s;
    setCode(s.code || '');
    dirty = false;
    const d = parseDirectives(s.code || '');
    asm = d.assembler || s.assembler || '';
    const bm = d.buildmode || s.buildmode || 'sna';
    buildmode = bm.startsWith('sna') ? (bm === 'sna' ? 'sna' : bm) : 'sna';
    entry = d.entryPoint || s.entry_point || '';
    await run(); // charge -> assemble -> envoie à l'émulateur en un clic
  }

  function newSource() {
    selected = { name: 'nouveau', author: null, description: null };
    setCode('; z80: assembler=rasm buildmode=sna entry=#8000\n  org #8000\nstart:\n  ret\n');
    dirty = false;
    asm = 'rasm'; buildmode = 'sna'; entry = '#8000';
  }

  async function run() {
    savedListScroll = listEl ? listEl.scrollTop : 0; // à préserver malgré le lancement de l'émulateur
    busy = true; logLines = []; log('Assemblage…');
    const cfg = { assembler: asm || undefined, buildmode, entryPoint: entry || undefined };
    const up = upsertDirectives(editor.value, cfg);  // maintien de la ligne ;z80:
    if (up !== editor.value) setCode(up);            // n'écrit que si ça change (évite la boucle auto)
    try {
      const t0 = performance.now();
      const res = await assemble({ code: editor.value, ...cfg }, factories);
      const dt = (performance.now() - t0).toFixed(0);
      preText = res.preprocessed || ''; // dispo même en cas d'échec (pour debug)
      lineOffset = res.lineOffset || 0;
      const built = res.ok && !!res.output;
      applyStatus(built); // met à jour l'indicateur (liste + source courante, + base en mode complet)
      for (const l of res.log.slice(-12)) log(l, 'muted', parseSourceLine(l));
      if (res.error) log('⚠ ' + res.error, 'err', parseSourceLine(res.error));
      if (!built) { log(`❌ Échec (${res.assembler}) — ${dt} ms`, 'err'); busy = false; return; }
      log(`✔ ${res.assembler} → .${res.ext} ${res.output.length} o en ${dt} ms`, 'ok');
      if (dlUrl) URL.revokeObjectURL(dlUrl);
      dlExt = res.ext || 'sna';
      dlUrl = URL.createObjectURL(new Blob([res.output], { type: 'application/octet-stream' }));
      if (amspiritEnabled && amspiritConnected && res.ext === 'sna') {
        try {
          const name = (selected?.name || 'z80next').replace(/[^\w.-]+/g, '_') + '.sna';
          await injectSna(amspiritUrl, res.output, { name, onLog: (m) => log('AMSpiriT: ' + m, 'muted') });
          log('✔ injecté dans AMSpiriT', 'ok');
        } catch (e) {
          amspiritConnected = false; // la sonde périodique retentera la connexion
          log('⚠ injection AMSpiriT échouée : ' + (e?.message || e), 'err');
        }
      } else {
        const url = await feedEmulator(res.output, res.ext);
        if (url) emuUrl = `/emu/tiny8bit/cpc.html?file=${encodeURIComponent(url)}`;
        else log('Service Worker indisponible — utilisez le téléchargement.', 'err');
      }
    } catch (e) { log('Erreur: ' + (e?.message || e), 'err'); }
    busy = false;
    await tick(); restoreListScroll(); // rétablit la position des vignettes après le rendu du panneau/iframe
  }

  async function save() {
    const data = { name: selected?.name || 'untitled', code: editor.value, assembler: asm || null,
      buildmode, entry_point: entry || null, author: selected?.author || null, description: selected?.description || null };
    const saved = selected?.id ? await store.update(selected.id, data) : await store.create(data);
    selected = saved; dirty = false; await refreshList(); log('💾 sauvegardé : ' + saved.name, 'ok');
  }
  async function fork() {
    if (!selected?.id) return;
    const f = await store.fork(selected.id, {}); selected = f; dirty = false; await refreshList(); log('⑂ fork : ' + f.name, 'ok');
  }

  // Met à jour l'indicateur d'assemblage (✅/❌) après un build, sans attendre un re-classify.
  function applyStatus(ok) {
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

  async function openPre() {
    if (!preText) return;
    showPre = true;
    await tick(); // attend le rendu de la modale avant de monter le viewer
    if (preEl) { preViewer?.destroy(); preViewer = makeViewer(preEl, preText); }
  }
  function closePre() { showPre = false; preViewer?.destroy(); preViewer = null; }
  async function copyPre() { try { await navigator.clipboard.writeText(preText); log('Copié.', 'ok'); } catch { log('Copie refusée par le navigateur.', 'err'); } }

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
    log(`Base : mode ${mode} — ${count} sources.`, 'muted');
    return () => { editor?.destroy(); clearInterval(amspiritTimer); };
  });

  const badge = (s) => s.build_status === 'ok' ? '✅' : s.build_status === 'external-dep' ? '📦' : '·';
  const fmtDate = (ms) => ms ? new Date(Number(ms)).toISOString().slice(0, 10) : '';
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
  }
}} />

<header>
  <strong>z80live</strong>
  <span class="mode" class:lite={mode === 'local'}>{mode === 'local' ? 'LITE (statique)' : mode === 'api' ? 'complet' : '…'}</span>
  <input class="search" placeholder="Rechercher…" bind:value={query} oninput={onSearch} />
  <label class="grp">grouper
    <select bind:value={groupBy}>
      <option value="">— liste à plat</option>
      <option value="genre">par genre</option>
      <option value="author">par auteur</option>
      <option value="group_name">par groupe</option>
      <option value="buildmode">par type de sortie</option>
      <option value="assembler">par assembleur</option>
    </select>
  </label>
  <span class="grow"></span>
  {#if amspiritEnabled}
    <span class="amsp" class:on={amspiritConnected} title={amspiritConnected ? 'AMSpiriT connecté : ' + amspiritUrl : 'AMSpiriT activé mais non joignable — repli sur le wasm intégré'}>
      ● AMSpiriT
    </span>
  {/if}
  <button class="ico" onclick={openAppSettings} title="Réglages de l'application">⚙️</button>
  {#if canWrite}<button onclick={newSource}>+ Nouveau</button>{/if}
</header>

<main class:no-list={!showList} class:no-emu={amspiritEnabled && amspiritConnected}>
  <button class="listtoggle" onclick={() => showList = !showList}
    title={showList ? 'Masquer la liste des sources' : 'Afficher la liste des sources'}>{showList ? '⟨' : '⟩'}</button>
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
      <div class="empty">Aucune source.</div>
    {/each}
  </aside>
  {/if}

  <section class="mid">
    <div class="controls">
      <button class="ico" onclick={openSettings} disabled={!selected} title="Réglages de la source">✏️</button>
      <button class="ico primary" onclick={run} disabled={busy} title="Assembler & exécuter">▶</button>
      <label class="auto" title="Réassembler automatiquement à chaque modification (500 ms)">
        <input type="checkbox" bind:checked={auto} onchange={onAutoToggle} /> auto
      </label>
      <span class="sep"></span>
      {#if canWrite}
        <button class="ico" class:dirty onclick={save} title={dirty ? 'Sauvegarder (modifications non enregistrées)' : 'Sauvegarder'}>💾</button>
        {#if selected?.id}<button class="ico" onclick={fork} title="Forker">⑂</button>{/if}
      {/if}
      {#if preText}<button class="ico" onclick={openPre} title="Code après préprocesseur">⧉</button>{/if}
      {#if dlUrl}<a class="ico dl" href={dlUrl} download={dlName} title="Télécharger le .{dlExt}">⬇</a>{/if}
    </div>

    {#if selected}
      <div class="meta">
        <span class="st" class:ok={selected.build_status === 'ok'} class:ko={selected.build_status === 'fail'}
          title="État d'assemblage">{selected.build_status === 'ok' ? '✅ assemble' : selected.build_status === 'fail' ? '❌ échoue' : selected.build_status === 'external-dep' ? '📦 dépend. externe' : '— non testé'}</span>
        <span class="t">{selected.name}</span>
        <span class="by">{selected.author ? 'par ' + selected.author : 'auteur inconnu'}</span>
        {#if selected.owner && selected.owner !== selected.author}<span class="tags">(owner: {selected.owner})</span>{/if}
        {#if selected.description}<div class="d">{selected.description}</div>{/if}
        <div class="tags">
          genre: {selected.genre || '(non classé)'}
          {#if selected.group_name} · groupe: {selected.group_name}{/if}
          {#if selected.assembler || asm} · asm: {selected.assembler || asm}{/if}
          {#if selected.updated_at} · maj: {fmtDate(selected.updated_at)}{/if}
        </div>
      </div>
    {/if}

    <div class="editor" bind:this={editorEl}></div>

    <pre class="log">{#each logLines as l}<span class={l.cls} class:goto={l.line != null} onclick={() => l.line != null && editor.gotoLine(l.line)}>{l.m}</span>{'\n'}{/each}</pre>
  </section>

  {#if !(amspiritEnabled && amspiritConnected)}
  <section class="emu">
    {#if emuUrl}<iframe title="émulateur" src={emuUrl} allow="autoplay; gamepad" onload={restoreListScroll}></iframe>
    {:else}<div class="ph">L'émulateur s'affichera ici après l'assemblage.</div>{/if}
  </section>
  {/if}
</main>

{#if showSettings && selected}
<div class="modal" onclick={applySettings} role="presentation">
  <div class="dialog small" onclick={(e) => e.stopPropagation()} role="dialog" aria-modal="true" tabindex="-1">
    <div class="dhead"><span>Réglages de la source</span><span class="grow"></span><button onclick={applySettings}>OK ✕</button></div>
    <div class="form">
      <label>Nom <input bind:value={selected.name} disabled={!canWrite} /></label>
      <label>Auteur <input bind:value={selected.author} disabled={!canWrite} /></label>
      <label class="wide">Description <input bind:value={selected.description} disabled={!canWrite} /></label>
      <label>Assembleur
        <select bind:value={asm}><option value="">auto (rasm)</option><option>rasm</option><option value="fantams">fantams</option><option>sjasmplus</option></select>
      </label>
      <label>Type de sortie
        <select bind:value={buildmode}><option>sna</option><option>sna_cpc6128</option><option>sna_cpc464</option></select>
      </label>
      <label>Point d'entrée <input bind:value={entry} placeholder="#8000" /></label>
      <label>Genre
        <select bind:value={selected.genre} disabled={!canWrite}>
          <option value={null}>(non classé)</option>
          {#each GENRES as gg}<option value={gg}>{gg}</option>{/each}
          {#if selected.genre && !GENRES.includes(selected.genre)}<option value={selected.genre}>{selected.genre}</option>{/if}
        </select>
      </label>
      {#if !canWrite}<div class="note wide">Mode lecture seule : l'assembleur / type / entrée s'appliquent à cette session ; le nom, l'auteur, la description et le genre ne sont pas sauvegardés.</div>{/if}
    </div>
  </div>
</div>
{/if}

{#if showAppSettings}
<div class="modal" onclick={closeAppSettings} role="presentation">
  <div class="dialog small" onclick={(e) => e.stopPropagation()} role="dialog" aria-modal="true" tabindex="-1">
    <div class="dhead"><span>Réglages</span><span class="grow"></span><button onclick={closeAppSettings}>OK ✕</button></div>
    <div class="form">
      <label class="wide chk">
        <input type="checkbox" bind:checked={amspiritEnabled} onchange={syncAmspiritPolling} />
        Piloter AMSpiriT (émulateur externe) au lieu du wasm intégré
      </label>
      <label class="wide">URL du serveur AMSpiriT
        <input bind:value={amspiritUrl} placeholder="http://127.0.0.1:8765" disabled={!amspiritEnabled} onchange={syncAmspiritPolling} />
      </label>
      <div class="wide note">
        {#if !amspiritEnabled}Émulateur wasm intégré (défaut).
        {:else if amspiritConnected}✅ connecté — le panel wasm est masqué, l'exécution passe par injection RAM + PC via l'API AMSpiriT.
        {:else}⏳ non joignable pour l'instant — repli sur l'émulateur wasm intégré tant que la connexion n'est pas établie.{/if}
      </div>
    </div>
  </div>
</div>
{/if}

{#if showPre}
<div class="modal" onclick={closePre} role="presentation">
  <div class="dialog" onclick={(e) => e.stopPropagation()} role="dialog" aria-modal="true" tabindex="-1">
    <div class="dhead">
      <span>Code transmis à l'assembleur (après préprocesseur)</span>
      <span class="grow"></span>
      <button onclick={copyPre}>Copier</button>
      <button onclick={closePre}>Fermer ✕</button>
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
  .mode.lite { border-color: #a83; color: #db8; }
  .search { flex: 0 1 260px; padding: .3rem .5rem; background: #22262a; color: #eee; border: 1px solid #444; border-radius: 5px; }
  .grow { flex: 1; }
  button, select, input, a.dl { font: inherit; background: #22262a; color: #eee; border: 1px solid #444; border-radius: 5px; padding: .3rem .5rem; cursor: pointer; text-decoration: none; }
  button.primary { background: #2d6; color: #052; border-color: #2d6; font-weight: 700; }
  button:disabled { opacity: .5; cursor: wait; }
  main { position: relative; display: grid; grid-template-columns: 240px 1fr 1fr; gap: .5rem; padding: .5rem; height: calc(100vh - 49px); box-sizing: border-box; overflow: hidden; }
  main.no-list { grid-template-columns: 1fr 1fr; }
  main.no-emu { grid-template-columns: 240px 1fr; }
  main.no-list.no-emu { grid-template-columns: 1fr; }
  .amsp { font-size: 11px; padding: .1rem .5rem; border: 1px solid #833; color: #e88; border-radius: 4px; }
  .amsp.on { border-color: #375; color: #7d9; }
  /* Les cellules doivent pouvoir rétrécir sous leur contenu (sinon débordement -> scroll de page). */
  .list, .mid, .emu { min-width: 0; min-height: 0; }
  .listtoggle { position: absolute; z-index: 5; top: .5rem; left: calc(.5rem + 240px); transform: translateX(-50%);
    width: 26px; height: 34px; padding: 0; font-size: 15px; line-height: 1; color: #9aa; }
  main.no-list .listtoggle { left: .5rem; transform: none; }
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
  .controls { display: flex; gap: .35rem; align-items: center; flex-wrap: wrap; }
  .controls label { font-size: 12px; color: #9aa; display: flex; gap: .3rem; align-items: center; }
  .ico { padding: .3rem .5rem; font-size: 14px; line-height: 1; }
  .ico.primary { background: #2d6; color: #052; border-color: #2d6; }
  .ico.dirty { background: #c33; color: #fff; border-color: #c33; }
  .auto { font-size: 12px; color: #9aa; }
  .sep { width: 1px; align-self: stretch; background: #333; margin: 0 .2rem; }
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
  .log { flex: 0 0 120px; overflow: auto; margin: 0; background: #0d0f10; border: 1px solid #333; border-radius: 6px; padding: 6px; font-size: 11.5px; white-space: pre-wrap; }
  .log .ok { color: #7f7; } .log .err { color: #f88; } .log .muted { color: #89a; }
  .log .goto { cursor: pointer; text-decoration: underline dotted; text-decoration-color: currentColor; }
  .log .goto:hover { background: #ffffff12; }
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
