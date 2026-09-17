package main

import (
	"context"
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

// App is the Wails-bound application object. Every exported method becomes
// callable from the frontend via the auto-generated `wailsjs/go/main/App.js`
// bindings.
type App struct {
	ctx      context.Context
	cfg      config.Config
	db       *db.DB
	pipeline *indexer.Pipeline
	search   *search.Engine
	ocr      ocr.Engine
	logger   *slog.Logger
}

// OnStartup is called by Wails at startup. We stash the context for later
// use (e.g. cancelling background work).
func (a *App) OnStartup(ctx context.Context) {
	a.ctx = ctx
	a.logger.Info("startup", "db", a.cfg.DBPath)
}

// OnShutdown is called by Wails when the window is closing.
func (a *App) OnShutdown(ctx context.Context) {
	a.logger.Info("shutdown")
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
	return a.db.ListFolders(a.ctx)
}

// RemoveFolder removes a folder and all its files from the index.
func (a *App) RemoveFolder(folderID int64) error {
	return a.db.RemoveFolder(folderID)
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

// Search runs a FTS5 query. See search.Engine.Search for query syntax.
func (a *App) Search(query string, limit int) ([]search.Hit, error) {
	return a.search.Search(a.ctx, query, limit)
}

// Suggestions returns autocomplete suggestions for a partial query.
func (a *App) Suggestions(prefix string, limit int) ([]string, error) {
	return a.search.Suggestions(a.ctx, prefix, limit)
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------

// GetFile returns the full file record (including extracted text).
func (a *App) GetFile(id int64) (*db.FileRecord, error) {
	return a.db.GetFile(id)
}

// OpenInExplorer reveals the file in Windows Explorer.
// (Parity: the C++ app does this via ShellExecute "explorer.exe /select,")
func (a *App) OpenInExplorer(path string) error {
	// Delegated to a small helper that wraps ShellExecute.
	return revealInExplorer(path)
}

// ---------------------------------------------------------------------------
// Indexing
// ---------------------------------------------------------------------------

// ExtractPending processes up to `limit` pending files. Returns the count
// actually processed. Useful as the "Extract Content Now" toolbar button.
func (a *App) ExtractPending(limit int) (int, error) {
	return a.pipeline.ExtractPending(a.ctx, limit)
}

// Stats returns aggregate counts for the dashboard.
func (a *App) Stats() (*db.Stats, error) {
	return a.db.GetStats(a.ctx)
}

// ---------------------------------------------------------------------------
// OCR (PoC: stub)
// ---------------------------------------------------------------------------

// OCRAvailable reports whether the OCR backend is usable on this machine.
func (a *App) OCRAvailable() bool {
	return a.ocr.Available()
}

// OCRSupportedLanguages returns the list of installed OCR language packs.
func (a *App) OCRSupportedLanguages() ([]string, error) {
	return a.ocr.SupportedLanguages()
}
