import { useState } from "react";
import {
  FileText,
  Folder,
  Clock,
  HardDrive,
  Hash,
  Loader2,
  AlertCircle,
  ExternalLink,
  Star,
  Tag,
} from "lucide-react";
import { useAppStore } from "@/stores/app-store";
import { api } from "@/lib/api";
import { cn, formatBytes, timeAgo } from "@/lib/utils";

export function PreviewPane() {
  const { selectedHit, selectedFile, loadingFile } = useAppStore();
  const [copied, setCopied] = useState(false);

  if (!selectedHit) {
    return (
      <div className="flex h-full flex-col items-center justify-center gap-3 p-8 text-center text-fg-subtle">
        <FileText size={40} />
        <div className="text-sm text-fg-muted">Select a result to preview</div>
      </div>
    );
  }

  if (loadingFile) {
    return (
      <div className="flex h-full items-center justify-center text-fg-subtle">
        <Loader2 className="animate-spin" size={20} />
      </div>
    );
  }

  const file = selectedFile;
  const text = file?.extracted_text || "";

  const handleCopy = async () => {
    if (!file?.extracted_text) return;
    try {
      await navigator.clipboard.writeText(file.extracted_text);
      setCopied(true);
      setTimeout(() => setCopied(false), 1500);
    } catch {
      // clipboard may not be available in Wails webview; fall back to selection.
    }
  };

  const handleOpenInExplorer = async () => {
    if (file) {
      try {
        await api.OpenInExplorer(file.path);
      } catch (e) {
        console.error(e);
      }
    }
  };

  return (
    <div className="flex h-full flex-col">
      {/* Header with file info + actions */}
      <div className="border-b border-border p-4">
        <div className="flex items-start gap-3">
          <FileText size={20} className="mt-0.5 shrink-0 text-accent" />
          <div className="flex-1 min-w-0">
            <h2 className="truncate text-base font-semibold">{selectedHit.file_name}</h2>
            <div
              className="mt-0.5 truncate text-xs text-fg-muted"
              title={selectedHit.path}
            >
              {selectedHit.path}
            </div>
          </div>
          <div className="flex shrink-0 items-center gap-1">
            <button
              onClick={handleCopy}
              className="rounded-md p-1.5 text-fg-muted hover:bg-bg-hover hover:text-fg"
              title="Copy extracted text"
            >
              {copied ? "Copied!" : <Hash size={15} />}
            </button>
            <button
              className="rounded-md p-1.5 text-fg-muted hover:bg-bg-hover hover:text-fg"
              title="Favorite (parity: not in PoC)"
            >
              <Star size={15} />
            </button>
            <button
              className="rounded-md p-1.5 text-fg-muted hover:bg-bg-hover hover:text-fg"
              title="Tags (parity: not in PoC)"
            >
              <Tag size={15} />
            </button>
            <button
              onClick={handleOpenInExplorer}
              className="rounded-md p-1.5 text-fg-muted hover:bg-bg-hover hover:text-fg"
              title="Show in Explorer"
            >
              <ExternalLink size={15} />
            </button>
          </div>
        </div>

        {/* Metadata grid */}
        {file && (
          <dl className="mt-3 grid grid-cols-2 gap-x-4 gap-y-1.5 text-xs">
            <Meta label="Size" value={formatBytes(file.size_bytes)} icon={<HardDrive size={11} />} />
            <Meta
              label="Modified"
              value={timeAgo(file.modified_time)}
              icon={<Clock size={11} />}
            />
            <Meta
              label="Type"
              value={file.extension.replace(/^\./, "").toUpperCase()}
            />
            <Meta
              label="Pages"
              value={file.page_count?.toString() ?? "—"}
            />
            <Meta
              label="Status"
              value={file.status}
              icon={
                file.status === "error" ? (
                  <AlertCircle size={11} className="text-red-400" />
                ) : undefined
              }
            />
            <Meta
              label="Folder"
              value={file.path.split(/[\\/]/).slice(-2, -1)[0] || ""}
              icon={<Folder size={11} />}
            />
          </dl>
        )}

        {file?.status === "error" && file.error_message && (
          <div className="mt-3 rounded-md border border-red-700/50 bg-red-950/40 px-3 py-2 text-xs text-red-200">
            <div className="font-medium">Extraction failed</div>
            <div className="mt-0.5 text-red-300/80">{file.error_message}</div>
          </div>
        )}
      </div>

      {/* Extracted text body */}
      <div className="scrollable flex-1 overflow-y-auto p-4">
        <div className="mb-2 flex items-center justify-between">
          <h3 className="text-xs font-medium uppercase tracking-wider text-fg-subtle">
            Extracted Text
          </h3>
          <span className="text-[10px] text-fg-subtle">
            {text.length.toLocaleString()} chars
          </span>
        </div>
        {text ? (
          <pre className="whitespace-pre-wrap break-words font-sans text-xs leading-relaxed text-fg">
            {text}
          </pre>
        ) : (
          <div className="text-xs text-fg-subtle">
            No text was extracted from this file. Possible reasons:
            <ul className="mt-2 ml-4 list-disc space-y-1">
              <li>It's a scanned PDF — OCR will be added in a later build.</li>
              <li>It's an image without OCR.</li>
              <li>The file is corrupted or empty.</li>
            </ul>
          </div>
        )}
      </div>
    </div>
  );
}

function Meta({
  label,
  value,
  icon,
}: {
  label: string;
  value: string;
  icon?: React.ReactNode;
}) {
  return (
    <div className="flex items-center gap-1.5">
      {icon && <span className="text-fg-subtle">{icon}</span>}
      <dt className="text-fg-subtle">{label}:</dt>
      <dd className="truncate font-medium text-fg">{value}</dd>
    </div>
  );
}
