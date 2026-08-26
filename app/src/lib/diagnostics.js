// Lecture des diagnostics d'assembleur : gravité, fichier, ligne.
//
// Les trois assembleurs écrivent la même information en TEXTE — c'est la seule chose
// qu'ils ont en commun, et donc la seule sur laquelle s'appuyer. Aucun n'expose de
// sortie structurée, et la couleur ANSI (rasm en émet) n'est pas de l'information :
// elle est retirée avant toute lecture.
//
// Module pur : ni DOM, ni état d'application. Tout ce qui concerne « quelle source de
// la base est ce fichier » vit ici aussi, parce que c'est la même question de lecture.

export const ANSI = /\x1b\[[0-9;]*m/g;
export const strip = (text) => String(text ?? '').replace(ANSI, '');

// Gravité d'une ligne de journal. Le mot porte l'information, jamais la couleur.
export function severity(text) {
  const t = strip(text);
  // sjasmplus termine par « Errors: 0 / Warnings: 0 » : le mot est là, le diagnostic non.
  if (/\b(errors|warnings)\s*:\s*0\b/i.test(t)) return 'muted';
  if (/\b(errors?|erreurs?|fatal)\b/i.test(t)) return 'err';
  if (/\b(warnings?|avertissements?)\b/i.test(t)) return 'warn';
  return 'muted';
}

// Fichier + ligne (1-indexée) cités par un diagnostic, dans la source telle que
// l'assembleur l'a lue — en-tête injecté compris. Trois formats :
//   rasm        Error: [lib/toolbox.asm:3] ...
//   sjasmplus   lib/toolbox.asm(3): error: ...
//   fantams     lib/toolbox.asm:3: error: ...
// Rend null si la ligne ne cite aucun emplacement.
export function parseSourceRef(text) {
  const t = strip(text);
  let m = /\[([^\]]*):(\d+)\]/.exec(t);
  if (!m) m = /^\s*([^\s:()]+)\((\d+)\):/.exec(t);
  if (!m) m = /^\s*([^\s:()]+):(\d+):/.exec(t);
  return m ? { file: m[1], line: parseInt(m[2], 10) } : null;
}

const baseName = (p) => String(p || '').replace(/^.*\//, '').toLowerCase();
const stem = (p) => baseName(p).replace(/\.[^.]*$/, '');

// Le fichier principal, sous lequel l'hôte écrit la source en cours d'édition.
export const MAIN_FILE = 'in.asm';
export const isMainFile = (file) => {
  const b = baseName(file);
  return !b || b === MAIN_FILE;
};

// Retrouve la librairie derrière le chemin qu'un assembleur cite. La comparaison porte
// sur le nom de fichier seul : l'assembleur cite le chemin tel qu'il l'a ouvert
// (« lib/toolbox.asm »), l'hôte connaît celui qu'il a écrit (« /lib/toolbox.asm »).
// Repli sur le nom sans extension, `filename` étant souvent stocké sans (« toolbox »).
// `pathOf` : la fonction qui donne son chemin virtuel à une librairie (includePath).
export function matchInclude(file, includes, pathOf) {
  if (isMainFile(file)) return null;
  const b = baseName(file);
  const list = includes || [];
  return list.find((i) => baseName(pathOf(i)) === b)
      || list.find((i) => stem(pathOf(i)) === stem(b))
      || null;
}
