// Éditeur CodeMirror 6 avec coloration Z80 (mode legacy), thème sombre.
import { EditorView, basicSetup } from 'codemirror';
import { EditorState } from '@codemirror/state';
import { StreamLanguage } from '@codemirror/language';
import { z80 } from '@codemirror/legacy-modes/mode/z80';

// Le mode amont ne reconnait comme litteral que "..." ; il ne matche pour '
// qu'un caractere unique (/\\?.'/), qu'il colorise en NOMBRE, et laisse le
// flux AU MILIEU d'un 'hello' — d'ou une ligne mal tokenisee en cascade.
//
// Dans fantams les deux delimiteurs designent le meme objet (ADR 0010), donc
// la meme couleur. On traite les litteraux nous-memes et on delegue le reste
// tel quel : forker la table de mnemoniques amont ne se justifierait pas.
const z80Strings = {
  ...z80,
  token(stream, state) {
    const q = stream.peek();
    if (q === '"' || q === "'") {
      stream.next();
      // Un litteral court jusqu'a la prochaine occurrence de SON delimiteur ;
      // l'autre y est un caractere ordinaire.
      let c;
      while ((c = stream.next()) != null) {
        if (c === q) break;
        if (c === '\\') stream.next();
      }
      return 'string'; // non termine compris : la couleur montre l'oubli
    }
    // Le mode amont ne reconnait pas les flottants (fantams, si) : sans ce garde,
    // "3.14" tokenise "3" en NOMBRE puis, au '.', bascule en etiquette locale —
    // les chiffres apres la virgule heritent alors de la couleur des labels.
    if (/\d/.test(q) && stream.match(/^\d+\.\d+([eE][+-]?\d+)?/)) return 'number';
    return z80.token(stream, state);
  },
};
import { keymap } from '@codemirror/view';
import { insertTab, indentLess } from '@codemirror/commands';
import { HighlightStyle, syntaxHighlighting } from '@codemirror/language';
import { tags } from '@lezer/highlight';

// basicSetup ne lie pas Tab : on l'ajoute pour insérer une tabulation (au lieu de sortir du focus).
const tabKeymap = keymap.of([{ key: 'Tab', run: insertTab }, { key: 'Shift-Tab', run: indentLess }]);

// Le style de coloration syntaxique PAR DEFAUT de CodeMirror (utilise en repli par
// basicSetup) est calibre pour un fond clair : certaines couleurs (ex. #00f sur les
// definitions, dont les nombres a virgule mal tokenises ci-dessus heritaient) sont
// illisibles sur notre fond sombre. On le remplace entierement plutot que de compter
// sur le fallback.
const z80Highlight = HighlightStyle.define([
  { tag: tags.keyword, color: '#ff9d5c' },
  { tag: [tags.number, tags.atom, tags.bool], color: '#a5d6ff' },
  { tag: [tags.string, tags.special(tags.string)], color: '#9fd88f' },
  { tag: tags.comment, color: '#6d7880', fontStyle: 'italic' },
  { tag: [tags.variableName, tags.definition(tags.variableName)], color: '#cfe3ff' },
  { tag: tags.special(tags.variableName), color: '#7cc9ff' },
  { tag: tags.invalid, color: '#ff6b6b' },
]);

const dark = EditorView.theme({
  '&': { color: '#cfe3ff', backgroundColor: '#0d0f10', height: '100%' },
  '.cm-content': { fontFamily: 'ui-monospace, monospace', fontSize: '12.5px' },
  '.cm-gutters': { backgroundColor: '#0d0f10', color: '#556', border: 'none' },
  '.cm-activeLine': { backgroundColor: '#ffffff08' },
  '.cm-activeLineGutter': { backgroundColor: '#ffffff10' },
  '&.cm-focused .cm-cursor': { borderLeftColor: '#7cf' },
  '.cm-selectionBackground, ::selection': { backgroundColor: '#2a4d6e' },
}, { dark: true });

export function makeEditor(parent, doc, onChange) {
  const view = new EditorView({
    parent,
    doc: doc || '',
    extensions: [
      basicSetup,
      tabKeymap,
      StreamLanguage.define(z80Strings),
      dark,
      syntaxHighlighting(z80Highlight),
      EditorView.updateListener.of((u) => { if (u.docChanged && onChange) onChange(view.state.doc.toString()); }),
    ],
  });
  return {
    get value() { return view.state.doc.toString(); },
    set value(v) { view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: v || '' } }); },
    // Ligne 1-indexée du curseur. La mise en forme n'est plus bijective sur les
    // lignes depuis qu'elle détache les labels (ADR 0017) : l'appelant corrige le
    // décalage, cf. `countDetachedBefore` dans App.svelte.
    get cursorLine() {
      return view.state.doc.lineAt(view.state.selection.main.head).number;
    },
    // Place le curseur sur la ligne 1-indexée `line1` et la fait défiler à l'écran.
    gotoLine(line1) {
      const doc = view.state.doc;
      const l = Math.max(1, Math.min(line1, doc.lines));
      const at = doc.line(l).from;
      view.dispatch({ selection: { anchor: at }, scrollIntoView: true });
      view.focus();
    },
    destroy() { view.destroy(); },
  };
}

// Vue en lecture seule (coloration Z80), texte sélectionnable/copiable. Pour le code préprocessé.
export function makeViewer(parent, doc) {
  const view = new EditorView({
    parent,
    doc: doc || '',
    extensions: [
      basicSetup,
      StreamLanguage.define(z80Strings),
      dark,
      syntaxHighlighting(z80Highlight),
      EditorState.readOnly.of(true),
      EditorView.editable.of(false),
    ],
  });
  return { destroy() { view.destroy(); } };
}
