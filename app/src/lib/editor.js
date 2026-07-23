// Éditeur CodeMirror 6 avec coloration Z80 (mode legacy), thème sombre.
import { EditorView, basicSetup } from 'codemirror';
import { EditorState } from '@codemirror/state';
import { StreamLanguage } from '@codemirror/language';
import { z80 } from '@codemirror/legacy-modes/mode/z80';

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
      StreamLanguage.define(z80),
      dark,
      EditorView.updateListener.of((u) => { if (u.docChanged && onChange) onChange(view.state.doc.toString()); }),
    ],
  });
  return {
    get value() { return view.state.doc.toString(); },
    set value(v) { view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: v || '' } }); },
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
      StreamLanguage.define(z80),
      dark,
      EditorState.readOnly.of(true),
      EditorView.editable.of(false),
    ],
  });
  return { destroy() { view.destroy(); } };
}
