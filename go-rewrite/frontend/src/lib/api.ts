// Type definitions matching the Go structs in app.go.
// Wails generates its own bindings at build time under ./wailsjs/go/main,
// but we use these for type-safety in our typed wrappers below.

export interface FolderRecord {
  id: number;
  path: string;
  added_at: string;
  last_scan_at?: string;
  enabled: boolean;
  file_count?: number;
}

export interface FileRecord {
  id: number;
  folder_id: number;
  path: string;
  file_name: string;
  extension: string;
  size_bytes: number;
  modified_time: string;
  indexed_time: string;
  sha256?: string;
  extracted_text?: string;
  ocr_text?: string;
  page_count?: number;
  status: "pending" | "extracted" | "error";
  error_message?: string;
  tags?: string[];
  note?: string;
}

export interface SearchHit {
  file_id: number;
  path: string;
  file_name: string;
  extension: string;
  size_bytes: number;
  modified_time: string;
  bm25_score: number;
  snippet: string;
}

export interface Stats {
  total_files: number;
  extracted: number;
  pending: number;
  errors: number;
  total_folders: number;
  index_size_mb: number;
}

export interface AddFolderRequest {
  path: string;
  extract_now?: boolean;
}

export interface AddFolderResponse {
  folder_id: number;
  scanned: number;
  extracted?: number;
}

// The Wails-generated binding object.
// In dev (without wails dev), this is undefined and we throw.
// The Wails runtime injects window.go when running under the Wails shell.
interface WailsBindings {
  AddFolder(req: AddFolderRequest): Promise<AddFolderResponse>;
  ListFolders(): Promise<FolderRecord[]>;
  RemoveFolder(id: number): Promise<void>;
  Search(query: string, limit: number): Promise<SearchHit[]>;
  Suggestions(prefix: string, limit: number): Promise<string[]>;
  GetFile(id: number): Promise<FileRecord | null>;
  OpenInExplorer(path: string): Promise<void>;
  ExtractPending(limit: number): Promise<number>;
  Stats(): Promise<Stats>;
  OCRAvailable(): Promise<boolean>;
  OCRSupportedLanguages(): Promise<string[]>;
}

function getBindings(): WailsBindings {
  // Wails v2 generates bindings at `window.go.main.App`.
  const w = window as unknown as {
    go?: { main?: { App: WailsBindings } };
  };
  if (w.go?.main?.App) {
    return w.go.main.App;
  }
  // Fallback for plain Vite dev (no Wails shell) — return a stub that
  // throws so the UI shows a clear "backend not available" message.
  const err = new Error(
    "DocuSearch backend not available. Run `wails dev` from the go-rewrite/ directory to start the desktop app."
  );
  return new Proxy(
    {},
    {
      get() {
        return () => Promise.reject(err);
      },
    }
  ) as WailsBindings;
}

export const api = getBindings();
