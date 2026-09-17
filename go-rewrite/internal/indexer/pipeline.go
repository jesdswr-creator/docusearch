// Package indexer walks folders and feeds files through the extraction pipeline.
package indexer

import (
	"context"
	"fmt"
	"io/fs"
	"log/slog"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/db"
	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/extract"
)

// Pipeline coordinates folder scanning + text extraction.
//
// It is intentionally single-instance for the PoC. The C++ app has a
// PriorityScheduler with thread pools; we'll add concurrency in a later
// iteration once we know which operations are actually the bottleneck.
type Pipeline struct {
	db        *db.DB
	extractor *extract.Registry
	logger    *slog.Logger
}

// NewPipeline constructs a Pipeline.
func NewPipeline(database *db.DB, logger *slog.Logger) *Pipeline {
	return &Pipeline{
		db:        database,
		extractor: extract.NewRegistry(),
		logger:    logger,
	}
}

// ScanFolder walks `folderPath`, upserts every indexable file, and returns
// the number of files enqueued for extraction.
//
// It does NOT run extraction — call ExtractPending() afterwards (or run
// them as separate background jobs in production).
func (p *Pipeline) ScanFolder(ctx context.Context, folderID int64, folderPath string) (int, error) {
	count := 0
	err := filepath.WalkDir(folderPath, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			// Don't abort the whole walk on a single unreadable entry.
			p.logger.Warn("walk error", "path", path, "err", err)
			return nil
		}
		if d.IsDir() {
			// Skip hidden / system directories.
			name := d.Name()
			if name == "$RECYCLE.BIN" || name == "System Volume Information" ||
				strings.HasPrefix(name, ".") {
				return filepath.SkipDir
			}
			return nil
		}
		if ctx.Err() != nil {
			return ctx.Err()
		}

		ext := strings.ToLower(filepath.Ext(path))
		if !p.extractor.IsIndexable(ext) {
			return nil
		}

		info, err := d.Info()
		if err != nil {
			return nil
		}

		rec := db.FileRecord{
			FolderID:     folderID,
			Path:         path,
			FileName:     d.Name(),
			Extension:    ext,
			SizeBytes:    info.Size(),
			ModifiedTime: info.ModTime().UTC().Format(time.RFC3339),
			Status:       "pending",
		}
		if _, err := p.db.UpsertFile(rec); err != nil {
			p.logger.Warn("upsert file failed", "path", path, "err", err)
			return nil
		}
		count++
		return nil
	})
	if err != nil {
		return count, fmt.Errorf("walk %s: %w", folderPath, err)
	}
	if err := p.db.MarkFolderScanned(folderID); err != nil {
		p.logger.Warn("mark folder scanned", "err", err)
	}
	return count, nil
}

// ExtractPending processes up to `limit` files with status='pending'.
// Returns the number of files actually processed.
//
// Errors per file are recorded on the file row; we keep going.
func (p *Pipeline) ExtractPending(ctx context.Context, limit int) (int, error) {
	files, err := p.db.PendingFiles(ctx, 0, limit)
	if err != nil {
		return 0, fmt.Errorf("fetch pending: %w", err)
	}
	for _, f := range files {
		if ctx.Err() != nil {
			return 0, ctx.Err()
		}
		p.extractOne(ctx, f)
	}
	return len(files), nil
}

func (p *Pipeline) extractOne(ctx context.Context, f db.FileRecord) {
	logger := p.logger.With("file_id", f.ID, "path", f.Path)

	// Skip if the file no longer exists (deleted between scan and extract).
	if _, err := os.Stat(f.Path); err != nil {
		if err := p.db.SetFileError(f.ID, "file missing: "+err.Error()); err != nil {
			logger.Warn("set file error", "err", err)
		}
		return
	}

	res, err := p.extractor.Extract(ctx, f.Path, f.Extension)
	if err != nil {
		logger.Warn("extract failed", "err", err)
		if err := p.db.SetFileError(f.ID, err.Error()); err != nil {
			logger.Warn("set file error", "err", err)
		}
		return
	}
	if err := p.db.SetFileExtracted(f.ID, res.Text, res.PageCount); err != nil {
		logger.Warn("set file extracted", "err", err)
		return
	}
	logger.Debug("extracted", "chars", len(res.Text), "method", res.Method)
}
