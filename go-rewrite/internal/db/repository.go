package db

import (
        "context"
        "database/sql"
        "errors"
        "fmt"
        "time"
)

// FileRecord is the Go representation of a row in `files`.
type FileRecord struct {
        ID             int64   `json:"id"`
        FolderID       int64   `json:"folder_id"`
        Path           string  `json:"path"`
        FileName       string  `json:"file_name"`
        Extension      string  `json:"extension"`
        SizeBytes      int64   `json:"size_bytes"`
        ModifiedTime   string  `json:"modified_time"`
        IndexedTime    string  `json:"indexed_time"`
        SHA256         *string `json:"sha256,omitempty"`
        ExtractedText  *string `json:"extracted_text,omitempty"`
        OCRText        *string `json:"ocr_text,omitempty"`
        PageCount      *int    `json:"page_count,omitempty"`
        Status         string  `json:"status"`
        ErrorMessage   *string `json:"error_message,omitempty"`
        // Joined fields (optional)
        Tags []string `json:"tags,omitempty"`
        Note *string  `json:"note,omitempty"`
}

// FolderRecord represents a row in `folders`.
type FolderRecord struct {
        ID          int64   `json:"id"`
        Path        string  `json:"path"`
        AddedAt     string  `json:"added_at"`
        LastScanAt  *string `json:"last_scan_at,omitempty"`
        Enabled     bool    `json:"enabled"`
        FileCount   int64   `json:"file_count,omitempty"`
}

// AddFolder inserts a folder row. Returns existing id if already present.
//
// Uses UPSERT + RETURNING (SQLite 3.35+, which go-sqlite3 ships with).
// QueryRow is used because Exec doesn't surface RETURNING rows.
func (d *DB) AddFolder(path string) (int64, error) {
        var id int64
        err := d.QueryRow(`
                INSERT INTO folders(path) VALUES (?)
                ON CONFLICT(path) DO UPDATE SET path = excluded.path
                RETURNING id`, path).Scan(&id)
        if err != nil {
                return 0, fmt.Errorf("add folder: %w", err)
        }
        return id, nil
}

// ListFolders returns all folders with their file counts.
func (d *DB) ListFolders(ctx context.Context) ([]FolderRecord, error) {
        rows, err := d.QueryContext(ctx, `
                SELECT f.id, f.path, f.added_at, f.last_scan_at, f.enabled,
                       COUNT(fi.id) AS file_count
                FROM folders f
                LEFT JOIN files fi ON fi.folder_id = f.id
                GROUP BY f.id
                ORDER BY f.added_at DESC
        `)
        if err != nil {
                return nil, err
        }
        defer rows.Close()

        // Initialize to empty (not nil) so JSON serialization returns []
        // instead of null. Otherwise the frontend crashes on .length.
        out := []FolderRecord{}
        for rows.Next() {
                var fr FolderRecord
                var lastScan sql.NullString
                if err := rows.Scan(&fr.ID, &fr.Path, &fr.AddedAt, &lastScan, &fr.Enabled, &fr.FileCount); err != nil {
                        return nil, err
                }
                if lastScan.Valid {
                        fr.LastScanAt = &lastScan.String
                }
                out = append(out, fr)
        }
        return out, rows.Err()
}

// RemoveFolder deletes a folder AND cascades to its files (FK on delete cascade).
func (d *DB) RemoveFolder(id int64) error {
        _, err := d.Exec(`DELETE FROM folders WHERE id = ?`, id)
        return err
}

// UpsertFile inserts or updates a file row by path. Returns the file id.
func (d *DB) UpsertFile(rec FileRecord) (int64, error) {
        res, err := d.Exec(`
                INSERT INTO files(folder_id, path, file_name, extension, size_bytes, modified_time, status)
                VALUES (?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(path) DO UPDATE SET
                        size_bytes=excluded.size_bytes,
                        modified_time=excluded.modified_time,
                        status='pending'
                WHERE files.modified_time != excluded.modified_time`,
                rec.FolderID, rec.Path, rec.FileName, rec.Extension, rec.SizeBytes, rec.ModifiedTime, rec.Status,
        )
        if err != nil {
                return 0, fmt.Errorf("upsert file: %w", err)
        }
        _ = res
        var id int64
        if err := d.QueryRow(`SELECT id FROM files WHERE path = ?`, rec.Path).Scan(&id); err != nil {
                return 0, err
        }
        return id, nil
}

// SetFileExtracted marks a file as extracted, populating text + page count.
func (d *DB) SetFileExtracted(id int64, text string, pageCount int) error {
        _, err := d.Exec(`
                UPDATE files
                SET extracted_text = ?, page_count = ?, status = 'extracted',
                    error_message = NULL, indexed_time = datetime('now')
                WHERE id = ?`, text, pageCount, id)
        return err
}

// SetFileError marks a file as failed.
func (d *DB) SetFileError(id int64, msg string) error {
        _, err := d.Exec(`
                UPDATE files SET status = 'error', error_message = ? WHERE id = ?`, msg, id)
        return err
}

// MarkFolderScanned stamps last_scan_at on a folder.
func (d *DB) MarkFolderScanned(id int64) error {
        _, err := d.Exec(`UPDATE folders SET last_scan_at = ? WHERE id = ?`,
                time.Now().UTC().Format(time.RFC3339), id)
        return err
}

// GetFile fetches a single file by id.
func (d *DB) GetFile(id int64) (*FileRecord, error) {
        row := d.QueryRow(`
                SELECT id, folder_id, path, file_name, extension, size_bytes,
                       modified_time, indexed_time, sha256, extracted_text, ocr_text,
                       page_count, status, error_message
                FROM files WHERE id = ?`, id)

        var rec FileRecord
        var sha, ext, ocr sql.NullString
        var pages sql.NullInt64
        var errMsg sql.NullString
        if err := row.Scan(&rec.ID, &rec.FolderID, &rec.Path, &rec.FileName, &rec.Extension,
                &rec.SizeBytes, &rec.ModifiedTime, &rec.IndexedTime, &sha, &ext, &ocr, &pages,
                &rec.Status, &errMsg); err != nil {
                if errors.Is(err, sql.ErrNoRows) {
                        return nil, nil
                }
                return nil, err
        }
        if sha.Valid {
                rec.SHA256 = &sha.String
        }
        if ext.Valid {
                rec.ExtractedText = &ext.String
        }
        if ocr.Valid {
                rec.OCRText = &ocr.String
        }
        if pages.Valid {
                p := int(pages.Int64)
                rec.PageCount = &p
        }
        if errMsg.Valid {
                rec.ErrorMessage = &errMsg.String
        }
        return &rec, nil
}

// PendingFiles returns files with status='pending' for a given folder (or all folders if folderID=0).
func (d *DB) PendingFiles(ctx context.Context, folderID int64, limit int) ([]FileRecord, error) {
        q := `
                SELECT id, folder_id, path, file_name, extension, size_bytes,
                       modified_time, indexed_time, status
                FROM files WHERE status = 'pending'`
        args := []any{}
        if folderID > 0 {
                q += ` AND folder_id = ?`
                args = append(args, folderID)
        }
        q += ` LIMIT ?`
        args = append(args, limit)

        rows, err := d.QueryContext(ctx, q, args...)
        if err != nil {
                return nil, err
        }
        defer rows.Close()

        // Initialize to empty (not nil) — see ListFolders for the JSON reason.
        out := []FileRecord{}
        for rows.Next() {
                var rec FileRecord
                if err := rows.Scan(&rec.ID, &rec.FolderID, &rec.Path, &rec.FileName,
                        &rec.Extension, &rec.SizeBytes, &rec.ModifiedTime, &rec.IndexedTime,
                        &rec.Status); err != nil {
                        return nil, err
                }
                out = append(out, rec)
        }
        return out, rows.Err()
}

// Stats holds a quick overview of the index.
type Stats struct {
        TotalFiles    int64 `json:"total_files"`
        Extracted     int64 `json:"extracted"`
        Pending       int64 `json:"pending"`
        Errors        int64 `json:"errors"`
        TotalFolders  int64 `json:"total_folders"`
        IndexSizeMB   float64 `json:"index_size_mb"`
}

// GetStats returns aggregate counts.
func (d *DB) GetStats(ctx context.Context) (*Stats, error) {
        var s Stats
        err := d.QueryRowContext(ctx, `
                SELECT
                        (SELECT COUNT(*) FROM files),
                        (SELECT COUNT(*) FROM files WHERE status='extracted'),
                        (SELECT COUNT(*) FROM files WHERE status='pending'),
                        (SELECT COUNT(*) FROM files WHERE status='error'),
                        (SELECT COUNT(*) FROM folders)
        `).Scan(&s.TotalFiles, &s.Extracted, &s.Pending, &s.Errors, &s.TotalFolders)
        if err != nil {
                return nil, err
        }
        return &s, nil
}
