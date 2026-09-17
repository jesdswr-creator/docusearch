import { useEffect } from "react";
import {
  FolderPlus,
  Folder,
  Trash2,
  Database,
  FileText,
  Clock,
  AlertCircle,
  Loader2,
} from "lucide-react";
import { useAppStore } from "@/stores/app-store";
import { cn, formatBytes } from "@/lib/utils";
import { Button } from "@/components/ui/button";
import { getRuntime } from "@/lib/api";

export function Sidebar() {
  const state = useAppStore();
  // Defensive: store should always have arrays, but coerce in case the
  // backend returns null (Go nil slice serializes to JSON null).
  const folders = state.folders ?? [];
  const stats = state.stats;
  const { refreshFolders, refreshStats, addFolder, removeFolder } = state;

  useEffect(() => {
    refreshFolders();
    refreshStats();
  }, [refreshFolders, refreshStats]);

  const handleAddFolder = async () => {
    // Use the Wails runtime's native folder picker. Falls back to a
    // text prompt if the runtime isn't available (e.g. running outside
    // the desktop shell, which shouldn't happen in production but is
    // useful for debugging).
    let path = "";
    const runtime = getRuntime();
    if (runtime?.OpenDirectoryDialog) {
      try {
        path = await runtime.OpenDirectoryDialog({ Title: "Select folder to index" });
      } catch (e) {
        // eslint-disable-next-line no-console
        console.error("OpenDirectoryDialog failed", e);
      }
    } else {
      // WebView2 doesn't have window.prompt — only use as last-resort fallback.
      path = window.prompt?.("Enter folder path to index:") ?? "";
    }
    if (!path) return;
    try {
      await addFolder(path, true);
    } catch (e) {
      // eslint-disable-next-line no-console
      console.error("Failed to add folder:", e);
      const rt = getRuntime();
      if (rt?.MessageDialog) {
        await rt.MessageDialog({
          Type: "error",
          Title: "Add folder failed",
          Message: (e as Error).message,
        });
      }
    }
  };

  return (
    <aside className="flex h-full w-64 flex-col border-r border-border bg-bg-subtle">
      {/* Brand */}
      <div className="flex items-center gap-2 px-4 py-3.5">
        <div className="flex h-7 w-7 items-center justify-center rounded-md bg-accent text-accent-fg">
          <Database size={16} />
        </div>
        <div className="flex flex-col">
          <span className="text-sm font-semibold">DocuSearch</span>
          <span className="text-[10px] text-fg-subtle">v2.0 PoC</span>
        </div>
      </div>

      <div className="px-3">
        <Button
          variant="default"
          size="sm"
          className="w-full justify-start gap-2"
          onClick={handleAddFolder}
        >
          <FolderPlus size={15} />
          Add Folder
        </Button>
      </div>

      {/* Folders list */}
      <div className="mt-4 flex-1 overflow-y-auto px-2">
        <div className="px-2 pb-1.5 text-[10px] font-medium uppercase tracking-wider text-fg-subtle">
          Indexed Folders
        </div>
        {folders.length === 0 ? (
          <div className="px-2 py-3 text-xs text-fg-subtle">
            No folders yet. Click <span className="text-fg">Add Folder</span> to start indexing.
          </div>
        ) : (
          <ul className="space-y-0.5">
            {folders.map((f) => (
              <li key={f.id}>
                <div className="group flex items-center gap-2 rounded-md px-2 py-1.5 hover:bg-bg-hover">
                  <Folder size={14} className="shrink-0 text-fg-muted" />
                  <div className="flex-1 truncate text-xs" title={f.path}>
                    {f.path}
                  </div>
                  <button
                    className="opacity-0 group-hover:opacity-100 text-fg-subtle hover:text-red-400"
                    onClick={async () => {
                      // Use Wails runtime dialog if available; fall back to window.confirm
                      // (which may not exist in WebView2 — the optional chaining handles that).
                      const rt = getRuntime();
                      let confirmed = false;
                      if (rt?.MessageDialog) {
                        const result = await rt.MessageDialog({
                          Type: "question",
                          Title: "Remove folder?",
                          Message: `Remove ${f.path} from the index? Files on disk are not affected.`,
                        });
                        confirmed = result === "Yes" || result === "yes";
                      } else {
                        confirmed = window.confirm?.(`Remove ${f.path} from index?`) ?? false;
                      }
                      if (confirmed) {
                        removeFolder(f.id);
                      }
                    }}
                    title="Remove"
                  >
                    <Trash2 size={13} />
                  </button>
                </div>
                {f.file_count !== undefined && f.file_count > 0 && (
                  <div className="px-2 pb-1 pl-8 text-[10px] text-fg-subtle">
                    {f.file_count} files
                  </div>
                )}
              </li>
            ))}
          </ul>
        )}
      </div>

      {/* Stats footer */}
      <div className="border-t border-border p-3 text-[11px] text-fg-muted">
        {stats ? (
          <div className="space-y-1">
            <StatRow icon={<FileText size={11} />} label="Files" value={stats.total_files} />
            <StatRow
              icon={<Loader2 size={11} className={stats.pending > 0 ? "animate-spin" : ""} />}
              label="Pending"
              value={stats.pending}
            />
            <StatRow
              icon={<AlertCircle size={11} className={stats.errors > 0 ? "text-red-400" : ""} />}
              label="Errors"
              value={stats.errors}
            />
            <StatRow
              icon={<Clock size={11} />}
              label="Index"
              value={formatBytes(stats.index_size_mb * 1024 * 1024)}
            />
          </div>
        ) : (
          <div className="text-fg-subtle">Loading stats…</div>
        )}
      </div>
    </aside>
  );
}

function StatRow({
  icon,
  label,
  value,
}: {
  icon: React.ReactNode;
  label: string;
  value: string | number;
}) {
  return (
    <div className="flex items-center gap-2">
      <span className={cn("text-fg-subtle")}>{icon}</span>
      <span className="flex-1">{label}</span>
      <span className="tabular-nums text-fg">{value}</span>
    </div>
  );
}
