// Typed Wails bindings wrapper.
//
// IMPORTANT (Wails v2 timing gotcha):
// Wails injects `window.go.main.App` ASYNCHRONOUSLY after the page loads.
// If we capture it once at module-load time, we'll often get undefined
// (because the JS bundle executes before Wails injects the bindings)
// and every API call will fail. The fix is to re-resolve on EVERY call.

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

// getBindings re-resolves window.go.main.App on every call. If Wails
// hasn't injected it yet, we throw — the caller's catch block will
// surface the error to the UI instead of crashing React's render.
function getBindings(): WailsBindings {
  const w = window as unknown as {
    go?: { main?: { App: WailsBindings } };
  };
  if (w.go?.main?.App) {
    return w.go.main.App;
  }
  throw new Error(
    "DocuSearch backend not available. If you're running outside the Wails desktop shell, that's expected — build the app with `wails build` and run the resulting .exe."
  );
}

// Lazy proxy: each property access re-resolves the bindings. This means
// the timing of Wails's binding injection no longer matters.
export const api: WailsBindings = new Proxy({} as WailsBindings, {
  get(_target, prop: string) {
    // Resolve on every access. If Wails hasn't injected yet, throw.
    const bindings = getBindings();
    const fn = (bindings as unknown as Record<string, unknown>)[prop];
    if (typeof fn !== "function") {
      return undefined;
    }
    return (fn as Function).bind(bindings);
  },
});

// Wails runtime — for OpenDirectoryDialog etc. Also lazy.
interface WailsRuntime {
  OpenDirectoryDialog(opts?: { Title?: string }): Promise<string>;
  MessageDialog(opts: { Type: string; Title: string; Message: string }): Promise<string>;
}

export function getRuntime(): WailsRuntime | null {
  const w = window as unknown as { runtime?: WailsRuntime };
  return w.runtime ?? null;
}
