-- DocuSearch schema v2 (Go rewrite)
-- SQLite + FTS5, mirrors the C++ schema with simplifications for the PoC.

PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA foreign_keys = ON;

-- Folders that the user has added to the index.
CREATE TABLE IF NOT EXISTS folders (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    path        TEXT NOT NULL UNIQUE,
    added_at    TEXT NOT NULL DEFAULT (datetime('now')),
    last_scan_at TEXT,
    enabled     INTEGER NOT NULL DEFAULT 1
);

-- Indexed files. One row per file.
CREATE TABLE IF NOT EXISTS files (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    folder_id       INTEGER NOT NULL REFERENCES folders(id) ON DELETE CASCADE,
    path            TEXT NOT NULL UNIQUE,
    file_name       TEXT NOT NULL,
    extension       TEXT NOT NULL,
    size_bytes      INTEGER NOT NULL,
    modified_time   TEXT NOT NULL,        -- ISO8601 from filesystem
    indexed_time    TEXT NOT NULL DEFAULT (datetime('now')),
    sha256          TEXT,                 -- populated by duplicate scanner later
    extracted_text  TEXT,                 -- raw extracted text (used to populate FTS)
    ocr_text        TEXT,                 -- OCR text (WinRT) - blank in PoC
    page_count      INTEGER,
    status          TEXT NOT NULL DEFAULT 'pending',  -- pending|extracted|error
    error_message   TEXT
);

CREATE INDEX IF NOT EXISTS idx_files_folder ON files(folder_id);
CREATE INDEX IF NOT EXISTS idx_files_ext ON files(extension);
CREATE INDEX IF NOT EXISTS idx_files_status ON files(status);

-- FTS5 virtual table for full-text search.
-- content is external-content-mapped to files.extracted_text + ocr_text + file_name.
CREATE VIRTUAL TABLE IF NOT EXISTS files_fts USING fts5(
    file_name,
    extracted_text,
    ocr_text,
    content=''
);

-- Triggers to keep FTS in sync with files table.
CREATE TRIGGER IF NOT EXISTS files_ai AFTER INSERT ON files BEGIN
    INSERT INTO files_fts(rowid, file_name, extracted_text, ocr_text)
    VALUES (new.id, new.file_name, COALESCE(new.extracted_text,''), COALESCE(new.ocr_text,''));
END;

CREATE TRIGGER IF NOT EXISTS files_ad AFTER DELETE ON files BEGIN
    DELETE FROM files_fts WHERE rowid = old.id;
END;

CREATE TRIGGER IF NOT EXISTS files_au AFTER UPDATE ON files BEGIN
    DELETE FROM files_fts WHERE rowid = old.id;
    INSERT INTO files_fts(rowid, file_name, extracted_text, ocr_text)
    VALUES (new.id, new.file_name, COALESCE(new.extracted_text,''), COALESCE(new.ocr_text,''));
END;

-- Tags (parity: present in C++ app).
CREATE TABLE IF NOT EXISTS tags (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    name    TEXT NOT NULL UNIQUE,
    color   TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS file_tags (
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    tag_id  INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    PRIMARY KEY (file_id, tag_id)
);

-- Notes per file (parity).
CREATE TABLE IF NOT EXISTS file_notes (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
    note    TEXT NOT NULL,
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Favorites (parity).
CREATE TABLE IF NOT EXISTS favorites (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
    added_at TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Saved searches (parity).
CREATE TABLE IF NOT EXISTS saved_searches (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    query       TEXT NOT NULL,
    created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Schema version for future migrations.
CREATE TABLE IF NOT EXISTS schema_meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

INSERT OR IGNORE INTO schema_meta(key, value) VALUES ('schema_version', '2');
