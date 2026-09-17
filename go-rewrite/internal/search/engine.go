// Package search implements FTS5-powered full-text search.
package search

import (
	"context"
	"database/sql"
	"fmt"
	"strings"

	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/db"
)

// Engine wraps a *db.DB and provides search operations.
type Engine struct {
	db *db.DB
}

// NewEngine constructs an Engine.
func NewEngine(database *db.DB) *Engine {
	return &Engine{db: database}
}

// Hit is a single search result.
type Hit struct {
	FileID      int64   `json:"file_id"`
	Path        string  `json:"path"`
	FileName    string  `json:"file_name"`
	Extension   string  `json:"extension"`
	SizeBytes   int64   `json:"size_bytes"`
	ModifiedTime string `json:"modified_time"`
	BM25Score   float64 `json:"bm25_score"`
	Snippet     string  `json:"snippet"`
}

// Search executes a FTS5 query and returns up to `limit` hits.
//
// Query syntax (FTS5 standard):
//   - plain words: foo bar
//   - phrase: "foo bar"
//   - AND / OR / NOT
//   - prefix: foo*
//   - column filter: file_name:report
//
// Unparseable queries are wrapped in quotes and retried as a phrase.
func (e *Engine) Search(ctx context.Context, rawQuery string, limit int) ([]Hit, error) {
	if limit <= 0 {
		limit = 50
	}
	q := sanitizeQuery(rawQuery)
	if q == "" {
		return nil, nil
	}

	// BM25 scoring favors rare terms. Negative because FTS5's BM25 returns
	// more-negative = more-relevant; we flip the sign so bigger = better
	// for the UI.
	const qSQL = `
		SELECT f.id, f.path, f.file_name, f.extension, f.size_bytes, f.modified_time,
		       -bm25(files_fts) AS score,
		       snippet(files_fts, 1, '<mark>', '</mark>', '…', 24) AS snippet
		FROM files_fts
		JOIN files f ON f.id = files_fts.rowid
		WHERE files_fts MATCH ?
		  AND f.status = 'extracted'
		ORDER BY score DESC
		LIMIT ?`

	rows, err := e.db.QueryContext(ctx, qSQL, q, limit)
	if err != nil {
		// Retry as phrase if the query syntax was invalid.
		phrase := `"` + strings.ReplaceAll(rawQuery, `"`, `""`) + `"`
		rows, err = e.db.QueryContext(ctx, qSQL, phrase, limit)
		if err != nil {
			return nil, fmt.Errorf("search: %w", err)
		}
	}
	defer rows.Close()

	var out []Hit
	for rows.Next() {
		var h Hit
		if err := rows.Scan(&h.FileID, &h.Path, &h.FileName, &h.Extension,
			&h.SizeBytes, &h.ModifiedTime, &h.BM25Score, &h.Snippet); err != nil {
			return nil, err
		}
		out = append(out, h)
	}
	return out, rows.Err()
}

// sanitizeQuery strips characters that would cause FTS5 syntax errors and
// collapses whitespace. It does NOT attempt to fully validate the query —
// invalid queries are caught at query time and retried as phrases.
func sanitizeQuery(q string) string {
	q = strings.TrimSpace(q)
	if q == "" {
		return ""
	}
	// Remove control characters.
	q = strings.Map(func(r rune) rune {
		if r < 0x20 {
			return -1
		}
		return r
	}, q)
	// Collapse whitespace.
	return strings.Join(strings.Fields(q), " ")
}

// Suggestions returns prefix-completion suggestions for an input string.
// Used by the search bar's autocomplete dropdown.
func (e *Engine) Suggestions(ctx context.Context, prefix string, limit int) ([]string, error) {
	prefix = strings.TrimSpace(prefix)
	if len(prefix) < 2 {
		return nil, nil
	}
	if limit <= 0 {
		limit = 10
	}

	// Pull distinct file_name tokens that start with the prefix.
	q := `
		SELECT DISTINCT file_name
		FROM files
		WHERE file_name LIKE ?
		ORDER BY file_name
		LIMIT ?`
	rows, err := e.db.QueryContext(ctx, q, prefix+"%", limit)
	if err != nil {
		return nil, fmt.Errorf("suggestions: %w", err)
	}
	defer rows.Close()

	var out []string
	for rows.Next() {
		var s string
		if err := rows.Scan(&s); err != nil {
			return nil, err
		}
		out = append(out, s)
	}
	return out, rows.Err()
}

// silence unused import warning if we drop the only use of sql package later.
var _ = sql.ErrNoRows
