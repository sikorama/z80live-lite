-- z80live — schéma SQLite (base portable, unique fichier)
-- Cible : compilation côté client (WASM), serveur = simple stockage/CRUD des sources.
-- Périmètre retenu : édition/fork en ligne. Pas de comptes, notes ni groupes.

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- Table principale des sources assembleur Z80.
CREATE TABLE IF NOT EXISTS sources (
  id            TEXT PRIMARY KEY,          -- conserve l'_id Mongo (continuité + lignée de fork)
  name          TEXT NOT NULL,
  slugname      TEXT,
  author        TEXT,                      -- champ informatif (plus d'auth)
  owner         TEXT,                      -- idem
  description   TEXT,
  category      TEXT,                      -- champ libre historique (notes)
  genre         TEXT,                      -- taxonomie contrôlée (jeu désassemblé, démo, tools…)
  group_name    TEXT,                      -- 'group' est un mot réservé SQL
  code          TEXT NOT NULL DEFAULT '',

  -- Options d'assemblage (extraites de buildOptions). NULL => défaut appliqué côté client.
  assembler     TEXT,                      -- 'rasm' | 'sjasmplus' | 'uz80' | NULL(=rasm)
  buildmode     TEXT,                      -- 'sna' | 'sna_cpc6128' | 'dsk' | ...
  entry_point   TEXT,
  start_point   TEXT,
  end_point     TEXT,
  command       TEXT,                      -- pour DSK (run"...)
  filename      TEXT,
  output_type   TEXT,

  -- Résultat de classification (rempli par classify.mjs) : aide au tri / masquage.
  build_status  TEXT,                      -- 'ok' | 'fail' | 'external-dep' | NULL
  compilable    INTEGER,                   -- 1 | 0 | NULL

  -- Lignée de fork (nouveau ; NULL pour l'import initial).
  fork_parent   TEXT REFERENCES sources(id) ON DELETE SET NULL,

  created_at    INTEGER,                   -- epoch ms
  updated_at    INTEGER,

  -- Zéro perte : document Mongo original complet (dont champs abandonnés : score, votes, user...).
  legacy_json   TEXT
);

CREATE INDEX IF NOT EXISTS idx_sources_name      ON sources(name);
CREATE INDEX IF NOT EXISTS idx_sources_buildmode ON sources(buildmode);
CREATE INDEX IF NOT EXISTS idx_sources_updated   ON sources(updated_at DESC);
CREATE INDEX IF NOT EXISTS idx_sources_fork       ON sources(fork_parent);

-- Recherche plein-texte (nom / auteur / description / code).
CREATE VIRTUAL TABLE IF NOT EXISTS sources_fts USING fts5(
  name, author, description, code,
  content='sources', content_rowid='rowid'
);

-- Triggers de synchro FTS.
CREATE TRIGGER IF NOT EXISTS sources_ai AFTER INSERT ON sources BEGIN
  INSERT INTO sources_fts(rowid, name, author, description, code)
  VALUES (new.rowid, new.name, new.author, new.description, new.code);
END;
CREATE TRIGGER IF NOT EXISTS sources_ad AFTER DELETE ON sources BEGIN
  INSERT INTO sources_fts(sources_fts, rowid, name, author, description, code)
  VALUES ('delete', old.rowid, old.name, old.author, old.description, old.code);
END;
CREATE TRIGGER IF NOT EXISTS sources_au AFTER UPDATE ON sources BEGIN
  INSERT INTO sources_fts(sources_fts, rowid, name, author, description, code)
  VALUES ('delete', old.rowid, old.name, old.author, old.description, old.code);
  INSERT INTO sources_fts(rowid, name, author, description, code)
  VALUES (new.rowid, new.name, new.author, new.description, new.code);
END;
