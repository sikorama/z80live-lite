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
  filename      TEXT,                      -- pour is_include=1 : nom/chemin virtuel (ex. 'lib/toolbox.asm')
                                            -- sous lequel ce fichier est injecté dans le FS wasm à l'assemblage
  output_type   TEXT,

  -- Profil/Fichier de lien/Format ne sont plus des colonnes de la Source (docs/adr/0002,
  -- tranche 2026-09-10) : ce sont des reglages de Cible/Membre. `default_target_id` designe
  -- la Cible implicite d'une Source seule (ADR 0002, Q1 : "une Source seule EST un Projet a
  -- un membre") — c'est elle qui porte desormais profil/format (Cible) et fichier de lien
  -- (Membre). NULL avant la premiere sauvegarde de ces reglages (source neuve, ou source
  -- jamais configuree). `sources.profile`/`ld_filename`/`container` ont existe (ADR 0001) et
  -- ont ete retires : docs/adr/0001 reste valable sur la directive ;z80:, plus sur leur lieu
  -- de stockage exact.
  default_target_id TEXT REFERENCES targets(id) ON DELETE SET NULL,

  -- 1 = librairie/fichier à inclure (pas de point d'entrée) : injecté automatiquement dans le
  -- répertoire de travail wasm de chaque assemblage, sous le nom `filename` (défaut: slug(name)+'.asm').
  is_include    INTEGER,

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

-- Le Projet (CONTEXT.md, docs/adr/0002) : la portee d'un build, TOUJOURS.
-- Regroupe des Sources point d'entree et porte une ou plusieurs Cibles
-- d'export ; chaque Cible a ses Membres (ADR 0002 : Projet > Cible > Membre).
CREATE TABLE IF NOT EXISTS projects (
  id            TEXT PRIMARY KEY,
  name          TEXT NOT NULL,
  created_at    INTEGER,
  updated_at    INTEGER
);

-- Une Cible d'export : ce qu'un Projet produit (un Artefact fantams — un
-- Conteneur comme CPR, une Base comme SNA). `profile` est partage par tous
-- ses Membres (un Conteneur cible une seule machine, ADR 0002).
CREATE TABLE IF NOT EXISTS targets (
  id            TEXT PRIMARY KEY,
  project_id    TEXT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
  name          TEXT NOT NULL,
  format        TEXT NOT NULL,                 -- 'sna' | 'cpr' (ADR 0002, Q2 : les seuls actifs)
  profile       TEXT,                          -- 'cpc6128' | 'cpcplus' | NULL
  created_at    INTEGER,
  updated_at    INTEGER
);
CREATE INDEX IF NOT EXISTS idx_targets_project ON targets(project_id);

-- Un Membre : une Source point d'entree affectee a UNE Cible, avec le role
-- que son Format y demande — `bank` (0..31) pour un Conteneur CPR, rien de
-- plus pour une Cible SNA (au plus un Membre, ADR 0002). `ld_filename` est
-- propre a CE placement dans CETTE Cible (la meme Source peut etre Membre
-- de deux Cibles avec deux scripts de lien differents).
CREATE TABLE IF NOT EXISTS target_members (
  target_id     TEXT NOT NULL REFERENCES targets(id) ON DELETE CASCADE,
  source_id     TEXT NOT NULL REFERENCES sources(id) ON DELETE CASCADE,
  bank          INTEGER,                        -- id physique de cartouche, 0..31 ; NULL hors CPR
  ld_filename   TEXT,
  PRIMARY KEY (target_id, source_id)
);
CREATE INDEX IF NOT EXISTS idx_target_members_source ON target_members(source_id);

CREATE INDEX IF NOT EXISTS idx_sources_name      ON sources(name);
CREATE INDEX IF NOT EXISTS idx_sources_buildmode ON sources(buildmode);
CREATE INDEX IF NOT EXISTS idx_sources_updated   ON sources(updated_at DESC);
CREATE INDEX IF NOT EXISTS idx_sources_fork       ON sources(fork_parent);
CREATE INDEX IF NOT EXISTS idx_sources_include     ON sources(is_include);

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
