import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// SPA statique. Les assets runtime (wasm, émulateur, sql.js) vivent dans public/ et sont
// référencés en chemins absolus (/wasm/, /emu/…) -> l'app doit être servie à la racine d'origine.
export default defineConfig({
  base: '/',
  plugins: [svelte()],
  server: {
    fs: { allow: ['..'] }, // autorise l'import des modules canoniques ../wasm et ../client
  },
  build: { outDir: 'dist', emptyOutDir: true, target: 'es2022' },
});
