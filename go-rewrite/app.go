package main

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"path/filepath"
	"strings"

	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/config"
	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/db"
	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/indexer"
	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/ocr"
	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/search"
)

// ErrNotReady is returned by any App method called before OnStartup
// has successfully opened the database. In normal operation this never
// happens — the UI loads after OnStartup completes — but it's a guard
// against racing during shutdown or partial-failure startup.
var ErrNotReady = errors.New("docusearch backend not ready (database not open)")

// App is the Wails-bound application object. Every exported method becomes
// callable from the frontend via the auto-generated `wailsjs/go/main/App.js`
// bindings.
//
// Lifecycle: constructed in main() with only cfg+logger wired up. The DB,
// search engine, indexer pipeline, and OCR engine are initialized in
// OnStartup and torn down in OnShutdown. This split is required because
// Wails runs main() during binding generation — any panic there breaks
// the build.
type App struct {
	ctx      context.Context
	cfg      config.Config
	logger   *slog.Logger
	// Lazy-initialized in OnStartup:
	db       *db.DB
	pipeline *indexer.Pipeline
	search   *search.Engine
	ocr      ocr.Engine
	// startupErr captures any error from OnStartup so we can surface it
	// to the UI instead of crashing.
	startupErr error
}

// OnStartup is called by Wails at startup. We stash the context for later
// use (e.g. cancelling background work) and open the database.
//
// If the DB fails to open we DON'T panic — we capture the error so the UI
// can render an error state. Wails binding generation also runs main() +
// OnStartup, so we must not panic here either (binding gen won't call
// OnStartup in practice, but defensive coding is cheap).
func (a *App) OnStartup(ctx context.Context) {
	a.ctx = ctx
	a.logger.Info("startup", "db", a.cfg.DBPath)

	database, err := db.Open(a.cfg.DBPath)
	if err != nil {
		a.logger.Error("open database", "err", err, "path", a.cfg.DBPath)
		a.startupErr = err
		return
	}
	a.db = database
	a.pipeline = indexer.NewPipeline(database, a.logger)
	a.search = search.NewEngine(database)
	a.ocr = ocr.New()
	a.logger.Info("database ready")
}

// OnShutdown is called by Wails when the window is closing.
func (a *App) OnShutdown(ctx context.Context) {
	a.logger.Info("shutdown")
	if a.db != nil {
		_ = a.db.Close()
	}
}

// ready returns ErrNotReady if OnStartup didn't successfully open the DB.
// Every method that touches the DB should call this first.
func (a *App) ready() error {
	if a.db == nil {
		if a.startupErr != nil {
			return fmt.Errorf("backend startup failed: %w", a.startupErr)
		}
		return ErrNotReady
	}
	return nil
}

// ---------------------------------------------------------------------------
// Folder operations
// ---------------------------------------------------------------------------

// AddFolderRequest is the body for AddFolder.
type AddFolderRequest struct {
	Path string `json:"path"`
	// ExtractNow, if true, runs extraction synchronously after the scan
	// (useful for "drag a folder in, see results immediately" UX).
	// For large folders, leave this false and rely on background extraction.
	ExtractNow bool `json:"extract_now"`
}

// AddFolderResponse is what AddFolder returns.
type AddFolderResponse struct {
	FolderID  int64 `json:"folder_id"`
	Scanned   int   `json:"scanned"`
	Extracted int   `json:"extracted,omitempty"`
}

// AddFolder registers a folder, scans it, and (optionally) extracts text.
func (a *App) AddFolder(req AddFolderRequest) (*AddFolderResponse, error) {
	if err := a.ready(); err != nil {
		return nil, err
	}
	path := strings.TrimSpace(req.Path)
	if path == "" {
		return nil, fmt.Errorf("path is required")
	}
	abs, err := filepath.Abs(path)
	if err != nil {
		return nil, fmt.Errorf("abs path: %w", err)
	}

	folderID, err := a.db.AddFolder(abs)
	if err != nil {
		return nil, fmt.Errorf("add folder: %w", err)
	}

	scanned, err := a.pipeline.ScanFolder(a.ctx, folderID, abs)
	if err != nil {
		return nil, err
	}

	resp := &AddFolderResponse{FolderID: folderID, Scanned: scanned}

	if req.ExtractNow {
		n, err := a.pipeline.ExtractPending(a.ctx, 10_000)
		if err != nil {
			a.logger.Warn("extract pending", "err", err)
		}
		resp.Extracted = n
	}
	return resp, nil
}

// ListFolders returns all indexed folders.
func (a *App) ListFolders() ([]db.FolderRecord, error) {
	if err := a.ready(); err != nil {
		return nil, err
	}
	return a.db.ListFolders(a.ctx)
}

// RemoveFolder removes a folder and all its files from the index.
func (a *App) RemoveFolder(folderID int64) error {
	if err := a.ready(); err != nil {
		return err
	}
	return a.db.RemoveFolder(folderID)
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

// Search runs a FTS5 query. See search.Engine.Search for query syntax.
func (a *App) Search(query string, limit int) ([]search.Hit, error) {
	if err := a.ready(); err != nil {
		return nil, err
	}
	return a.search.Search(a.ctx, query, limit)
}

// Suggestions returns autocomplete suggestions for a partial query.
func (a *App) Suggestions(prefix string, limit int) ([]string, error) {
	if err := a.ready(); err != nil {
		return nil, err
	}
	return a.search.Suggestions(a.ctx, prefix, limit)
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------

// GetFile returns the full file record (including extracted text).
func (a *App) GetFile(id int64) (*db.FileRecord, error) {
	if err := a.ready(); err != nil {
		return nil, err
	}
	return a.db.GetFile(id)
}

// OpenInExplorer reveals the file in Windows Explorer.
// (Parity: the C++ app does this via ShellExecute "explorer.exe /select,")
func (a *App) OpenInExplorer(path string) error {
	// No DB dependency — safe to call without ready() check.
	return revealInExplorer(path)
}

// ---------------------------------------------------------------------------
// Indexing
// ---------------------------------------------------------------------------

// ExtractPending processes up to `limit` pending files. Returns the count
// actually processed. Useful as the "Extract Content Now" toolbar button.
func (a *App) ExtractPending(limit int) (int, error) {
	if err := a.ready(); err != nil {
		return 0, err
	}
	return a.pipeline.ExtractPending(a.ctx, limit)
}

// Stats returns aggregate counts for the dashboard.
func (a *App) Stats() (*db.Stats, error) {
	if err := a.ready(); err != nil {
		return nil, err
	}
	return a.db.GetStats(a.ctx)
}

// ---------------------------------------------------------------------------
// OCR (PoC: stub)
// ---------------------------------------------------------------------------

// OCRAvailable reports whether the OCR backend is usable on this machine.
func (a *App) OCRAvailable() bool {
	// OCR engine is a Noop in PoC — always false. The check doesn't need
	// the DB, but we still wait for OnStartup so a.ocr is non-nil.
	if a.ocr == nil {
		return false
	}
	return a.ocr.Available()
}

// OCRSupportedLanguages returns the list of installed OCR language packs.
func (a *App) OCRSupportedLanguages() ([]string, error) {
	if a.ocr == nil {
		return nil, ErrNotReady
	}
	return a.ocr.SupportedLanguages()
}
