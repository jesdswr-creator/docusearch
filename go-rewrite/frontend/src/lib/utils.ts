import { type ClassValue, clsx } from "clsx";
import { twMerge } from "tailwind-merge";

// Standard shadcn-style className helper.
export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs));
}

// Human-readable file size.
export function formatBytes(bytes: number): string {
  if (bytes === 0) return "0 B";
  const k = 1024;
  const sizes = ["B", "KB", "MB", "GB", "TB"];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  const v = bytes / Math.pow(k, i);
  return `${v.toFixed(v < 10 && i > 0 ? 1 : 0)} ${sizes[i]}`;
}

// Relative time from an ISO string.
export function timeAgo(iso: string): string {
  const then = new Date(iso).getTime();
  if (Number.isNaN(then)) return iso;
  const sec = Math.floor((Date.now() - then) / 1000);
  if (sec < 60) return "just now";
  const min = Math.floor(sec / 60);
  if (min < 60) return `${min}m ago`;
  const hr = Math.floor(min / 60);
  if (hr < 24) return `${hr}h ago`;
  const day = Math.floor(hr / 24);
  if (day < 30) return `${day}d ago`;
  return new Date(iso).toLocaleDateString();
}

// Get an icon key from a file extension.
export function iconForExtension(ext: string): string {
  const e = ext.toLowerCase().replace(/^\./, "");
  if (e === "pdf") return "file-text";
  if (["doc", "docx"].includes(e)) return "file-text";
  if (["xls", "xlsx", "xlsm", "csv"].includes(e)) return "sheet";
  if (["ppt", "pptx"].includes(e)) return "presentation";
  if (["txt", "md", "log", "rtf"].includes(e)) return "file";
  if (["jpg", "jpeg", "png", "gif", "bmp", "tiff", "webp"].includes(e)) return "image";
  return "file";
}
