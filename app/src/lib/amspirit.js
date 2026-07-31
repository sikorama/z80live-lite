// Client pour piloter un émulateur AMSpiriT-lite externe via son API HTTP locale
// (par défaut http://127.0.0.1:8765), en alternative à l'émulateur wasm intégré.
// Doc de l'API : web_server_api.md (dépôt amspirit-lite).

const trimBase = (base) => (base || '').replace(/\/+$/, '');

// Sonde de connectivité légère (timeout court : appelée en polling depuis l'UI).
export async function ping(base, { timeoutMs = 800 } = {}) {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), timeoutMs);
  try {
    const res = await fetch(trimBase(base) + '/api/ping', { signal: ctrl.signal });
    return res.ok;
  } catch {
    return false;
  } finally {
    clearTimeout(t);
  }
}

// Envoie un .sna à AMSpiriT via /api/media : une seule requête, format autodétecté par la
// signature "MV - SNA", état machine complet restauré côté serveur (registres + RAM). Remplace
// l'ancienne approche pause + écriture RAM par blocs + redirection PC, qui butait sur le pause
// côté AMSpiriT ne tenant pas de façon fiable.
export async function injectSna(base, snaBytes, { name = 'z80next.sna', onLog } = {}) {
  const bytes = snaBytes instanceof Uint8Array ? snaBytes : new Uint8Array(snaBytes);
  const log = onLog || (() => {});

  log(`envoi de ${bytes.length} o vers /api/media…`);
  const res = await fetch(`${trimBase(base)}/api/media?${new URLSearchParams({ name })}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/octet-stream' },
    body: bytes,
  });
  if (!res.ok) throw new Error(`/api/media → HTTP ${res.status}`);
  const json = await res.json();
  log('chargé');
  return json;
}
