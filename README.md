# z80next — serverless architecture for z80live

Rework of z80live into a simple, portable architecture:

- **Client-side compilation**: assemblers (rasm, sjasmplus) run in **WebAssembly** in the browser. No more build server (`z80live_node` removed).
- **Client-side emulation**: tiny8bit / RVMPlayer / crocods in WASM, fed directly from the `.sna` via a blob (no server URL involved).
- **Server = simple database**: a portable **SQLite** file + an all-JS CRUD API. No MongoDB, no Meteor.
- **Frontend**: **Vite + Svelte 5 SPA** (`app/`), client only, static build. CodeMirror 6 editor (Z80 mode).

Functional scope kept: **online editing / forking**. Dropped: accounts/login, notes/votes, groups/permissions. Public reads, open writes (protectable via `Z80_WRITE_TOKEN`).

> Practical guide (local dev, deployment, bulk `.sna` export): **[QUICKSTART.md](QUICKSTART.md)**.

## Structure

```
z80next/
├── db/          schema.sql · import.mjs · classify.mjs · z80live.sqlite (generated, ~39 MB)
├── server/      api.mjs  — REST CRUD/fork + serves the SPA (node:http + node:sqlite, 0 dependencies)
├── wasm/        rasm.mjs+.wasm · sjasmplus.mjs+.wasm · assemble.mjs (isomorphic module)
├── client/      store.mjs  — unified access layer (API or local SQLite/sql.js)
├── emu/         tiny8bit/  — CPC emulator (WASM)
├── emu-sw.js    Service Worker: serves the assembled .sna at /build/<name>.sna (no server needed)
├── vendor/      sqljs/     — SQLite WASM for browser reads (lite mode)
├── app/         Vite + Svelte SPA (UI) — builds -> app/dist/
└── scripts/     export-lite.mjs  — generates the distributable static bundle
```

## Usage (full mode: editing)

Requires Node ≥ 22.5 (`node:sqlite`). Compatible with Bun (`bun:sqlite`).

```bash
npm run import              # (re)generates db/z80live.sqlite from ../export_full.json
(cd app && npm install && npm run build)   # builds the SPA -> app/dist
npm run serve               # API + SPA on http://localhost:3000
```

The server serves `app/dist` if present (SPA), otherwise `demo/` (raw demo, no build).

Env vars: `PORT`, `DB` (path to the .sqlite file), `Z80_WRITE_TOKEN` (if set, writes require `Authorization: Bearer <token>`).

## API

| Method | Route | Purpose |
|---|---|---|
| GET | `/api/sources?q=&buildmode=&limit=&offset=` | list / FTS search (without code) |
| GET | `/api/sources/:id` | full source (with code) |
| POST | `/api/sources` | create |
| PUT | `/api/sources/:id` | update |
| POST | `/api/sources/:id/fork` | fork (lineage via `fork_parent`) |
| DELETE | `/api/sources/:id` | delete |
| GET | `/api/includes` | library sources (`is_include=1`), with their code — injected into the wasm FS at assemble time |
| GET | `/api/health` | health check |

## Demo — the full loop (edit → assemble → run)

```bash
npm run serve            # API + static files (demo, wasm, emulator) on a single origin
# then open http://localhost:3000/  in a browser
```

`demo/index.html` wires up the whole chain **without a build server**: select a source (API) →
editor → `rasm`/`sjasmplus` assembly in WASM (`wasm/assemble.mjs`) → the `.sna` (Uint8Array) is
passed as a **blob** to the tiny8bit emulator (`emu/tiny8bit/cpc.html?file=<blob>`), same origin.

- `wasm/rasm.mjs` + `rasm.wasm` — rasm in WASM (byte-correct output; minor documented memory discrepancy).
- `wasm/sjasmplus.mjs` + `sjasmplus.wasm` — sjasmplus in WASM (output identical to native).
- `wasm/assemble.mjs` — isomorphic module: header/footer per buildOptions, defaults to rasm, falls back to sjasmplus.
- **Library / include sources**: a source can be flagged `is_include=1` (no entry point) to act as a
  library. Before assembling, `wasm/assemble.mjs` writes every such source's code into the wasm
  virtual filesystem under a path derived from its `filename` column (default: slugified name +
  `.asm`), so the main source's `INCLUDE`/`READ`/`INCBIN` directives can resolve it. The list of
  library sources is fetched via `store.listIncludes()` / `GET /api/includes`.
- `emu/tiny8bit/` — CPC emulator (floooh/tiny8bit), args `file`/`input`/`joystick`/`type`.

> ⚠️ *Visual* validation of the emulator happens in a browser (not testable headless).
> Dev note: the assemblers are built with `-sSTACK_SIZE=8388608` (8 MB). The Emscripten default
> (64 KB) caused a stack overflow in rasm's recursive-descent parser → `table index out of
> bounds` trap (including in the browser). `assemble.mjs` surfaces the error via `res.error`.

## Lite version (static, distributable, read-only)

The SQLite database can be **embedded in the client** and read locally (sql.js): no server at all.
The full version (with API) serves as the editing instance; a static snapshot is **exported** from it.

```bash
(cd app && npm run build)   # up-to-date SPA
npm run export:lite         # generates ../dist-lite/  (~7.4 MB: SPA + assemblers + emulator + gzipped database)
cd ../dist-lite && npx serve   # or any static host / CDN
```

- **The same SPA** serves both modes: `client/store.mjs#openStore` detects the API; otherwise
  it loads `db/z80live.sqlite.gz` (a lightened database without FTS or `legacy_json`, gzipped ~4.8 MB) via **sql.js**.
  The CDN build of sql.js lacks FTS5 → lite search falls back to **LIKE**.
- **Serverless emulator**: `emu-sw.js` (Service Worker) serves the assembled `.sna` at `/build/<name>.sna`
  (URL with extension → type detection by the emulator). Also works in full mode.
- Read-only: no source saving (editing/running are ephemeral only).

## Bulk .sna export

Generates the `.sna` for every SNA source that compiles (replays rasm/sjasmplus in WASM on the Node
side, same logic as `db/classify.mjs`), into a folder or a single zip archive.
Step-by-step guide (and deployment): [QUICKSTART.md](QUICKSTART.md).

```bash
npm run export:sna              # -> ../sna-export/ (one .sna per source)
npm run export:sna:zip          # -> ../sna-export.zip (single archive)
# or with an explicit destination:
node --experimental-sqlite scripts/export-sna.mjs my-folder
node --experimental-sqlite scripts/export-sna.mjs --zip my-archive.zip
```

Sources that fail to assemble (external `incbin`/`include` dependencies, syntax errors) are
skipped; a summary (`OK`/failures) is printed at the end of the run.

## TODO

- Dedicated syntax highlighting for the `;z80:` directive line in the editor.
- Fix rasm's memory discrepancy (dependency on zeroed malloc) for byte-perfect determinism.
- Build sjasmplus **with Lua** (`USE_LUA=1`) for the ~6 sources that script in Lua.
- Pre-compiled `.sna` fallback for the ~15 sources with external dependencies (`incbin`/`include`).
- DSK support (deferred).
