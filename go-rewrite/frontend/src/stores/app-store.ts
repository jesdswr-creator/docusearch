import { create } from "zustand";
import { api, type SearchHit, type FileRecord, type Stats, type FolderRecord } from "@/lib/api";

interface AppState {
  // Search
  query: string;
  hits: SearchHit[];
  searching: boolean;
  searchError: string | null;

  // Selection
  selectedHit: SearchHit | null;
  selectedFile: FileRecord | null;
  loadingFile: boolean;

  // Sidebar
  folders: FolderRecord[];
  stats: Stats | null;

  // Actions
  setQuery: (q: string) => void;
  runSearch: () => Promise<void>;
  selectHit: (hit: SearchHit | null) => Promise<void>;
  refreshFolders: () => Promise<void>;
  refreshStats: () => Promise<void>;
  addFolder: (path: string, extractNow: boolean) => Promise<void>;
  removeFolder: (id: number) => Promise<void>;
  extractPending: () => Promise<number>;
}

export const useAppStore = create<AppState>((set, get) => ({
  query: "",
  hits: [],
  searching: false,
  searchError: null,
  selectedHit: null,
  selectedFile: null,
  loadingFile: false,
  folders: [],
  stats: null,

  setQuery: (q) => set({ query: q }),

  runSearch: async () => {
    const { query } = get();
    if (!query.trim()) {
      set({ hits: [], searchError: null });
      return;
    }
    set({ searching: true, searchError: null });
    try {
      // Coerce null -> []: Go serializes nil slices as JSON null, which
      // would crash .length / .map on the consumer.
      const hits = (await api.Search(query, 100)) ?? [];
      set({ hits, searching: false });
    } catch (e) {
      set({ searching: false, searchError: (e as Error).message });
    }
  },

  selectHit: async (hit) => {
    set({ selectedHit: hit, selectedFile: null, loadingFile: hit !== null });
    if (!hit) return;
    try {
      const file = await api.GetFile(hit.file_id);
      set({ selectedFile: file, loadingFile: false });
    } catch (e) {
      set({ loadingFile: false });
      console.error("GetFile failed", e);
    }
  },

  refreshFolders: async () => {
    try {
      // Coerce null -> [] (Go nil slice JSON serialization).
      const folders = (await api.ListFolders()) ?? [];
      set({ folders });
    } catch (e) {
      console.error("ListFolders failed", e);
    }
  },

  refreshStats: async () => {
    try {
      const stats = await api.Stats();
      set({ stats });
    } catch (e) {
      console.error("Stats failed", e);
    }
  },

  addFolder: async (path, extractNow) => {
    await api.AddFolder({ path, extract_now: extractNow });
    await get().refreshFolders();
    await get().refreshStats();
  },

  removeFolder: async (id) => {
    await api.RemoveFolder(id);
    await get().refreshFolders();
    await get().refreshStats();
  },

  extractPending: async () => {
    const n = await api.ExtractPending(10000);
    await get().refreshStats();
    return n;
  },
}));
