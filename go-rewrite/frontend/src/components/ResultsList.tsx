import { FileText, File, Sheet, Presentation, Image as ImageIcon, Star } from "lucide-react";
import type { SearchHit } from "@/lib/api";
import { useAppStore } from "@/stores/app-store";
import { cn, formatBytes, timeAgo } from "@/lib/utils";

function IconFor({ ext }: { ext: string }) {
  const e = ext.toLowerCase().replace(/^\./, "");
  const cls = "shrink-0 text-fg-muted";
  if (e === "pdf") return <FileText size={16} className={cn(cls, "text-red-400")} />;
  if (["doc", "docx"].includes(e)) return <FileText size={16} className={cn(cls, "text-blue-400")} />;
  if (["xls", "xlsx", "xlsm", "csv"].includes(e))
    return <Sheet size={16} className={cn(cls, "text-green-400")} />;
  if (["ppt", "pptx"].includes(e))
    return <Presentation size={16} className={cn(cls, "text-orange-400")} />;
  if (["jpg", "jpeg", "png", "gif", "bmp", "tiff", "webp"].includes(e))
    return <ImageIcon size={16} className={cn(cls, "text-purple-400")} />;
  return <File size={16} className={cls} />;
}

export function ResultsList() {
  const { hits, selectedHit, selectHit, searching, query } = useAppStore();

  if (searching) {
    return (
      <div className="flex h-full items-center justify-center text-sm text-fg-subtle">
        Searching…
      </div>
    );
  }

  if (!query.trim()) {
    return (
      <div className="flex h-full flex-col items-center justify-center gap-3 p-8 text-center">
        <FileText size={40} className="text-fg-subtle" />
        <div className="text-sm text-fg-muted">
          Start typing to search across your indexed documents.
        </div>
        <div className="text-xs text-fg-subtle">
          Supports phrases ("foo bar"), boolean (AND / OR / NOT), and prefix (foo*)
        </div>
      </div>
    );
  }

  if (hits.length === 0) {
    return (
      <div className="flex h-full flex-col items-center justify-center gap-2 p-8 text-center">
        <div className="text-sm text-fg-muted">No results for "{query}"</div>
        <div className="text-xs text-fg-subtle">
          Try fewer words, or use a prefix search like "{query.split(" ")[0]}*"
        </div>
      </div>
    );
  }

  return (
    <div className="scrollable h-full overflow-y-auto">
      <div className="border-b border-border px-4 py-2 text-xs text-fg-subtle">
        {hits.length} result{hits.length === 1 ? "" : "s"}
      </div>
      <ul className="divide-y divide-border">
        {hits.map((hit) => (
          <li key={hit.file_id}>
            <button
              onClick={() => selectHit(hit)}
              className={cn(
                "flex w-full flex-col gap-1 px-4 py-2.5 text-left transition-colors",
                selectedHit?.file_id === hit.file_id
                  ? "bg-accent/10 border-l-2 border-l-accent"
                  : "hover:bg-bg-hover border-l-2 border-l-transparent"
              )}
            >
              <div className="flex items-center gap-2">
                <IconFor ext={hit.extension} />
                <span className="flex-1 truncate text-sm font-medium">
                  {hit.file_name}
                </span>
                <span className="shrink-0 text-[10px] tabular-nums text-fg-subtle">
                  {hit.bm25_score.toFixed(2)}
                </span>
              </div>
              {hit.snippet && (
                <div
                  className="line-clamp-2 text-xs text-fg-muted"
                  dangerouslySetInnerHTML={{ __html: hit.snippet }}
                />
              )}
              <div className="flex items-center gap-3 text-[10px] text-fg-subtle">
                <span className="truncate" title={hit.path}>
                  {hit.path}
                </span>
                <span className="shrink-0">{formatBytes(hit.size_bytes)}</span>
                <span className="shrink-0">{timeAgo(hit.modified_time)}</span>
              </div>
            </button>
          </li>
        ))}
      </ul>
    </div>
  );
}
