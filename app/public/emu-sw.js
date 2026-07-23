// Service Worker : sert les binaires assemblés (.sna/.dsk) à une URL /build/<name>.<ext>
// depuis la mémoire, sans serveur. L'émulateur détecte le type par l'extension de l'URL.
// Fonctionne en mode statique (lite) comme avec l'API. Scope '/' (placé à la racine).
const builds = new Map();

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (e) => e.waitUntil(self.clients.claim()));

self.addEventListener('message', (e) => {
  const d = e.data || {};
  if (d.type === 'put-build') {
    builds.set(d.name, d.bytes);
    // borne mémoire
    if (builds.size > 16) builds.delete(builds.keys().next().value);
    e.source && e.source.postMessage({ type: 'build-ready', name: d.name });
  }
});

self.addEventListener('fetch', (e) => {
  const m = new URL(e.request.url).pathname.match(/\/build\/([\w.-]+)$/);
  if (m && builds.has(m[1])) {
    e.respondWith(new Response(builds.get(m[1]), {
      headers: {
        'Content-Type': 'application/octet-stream',
        'Content-Disposition': `attachment; filename="${m[1]}"`,
      },
    }));
  }
});
