import { Search, Loader2, X } from "lucide-react";
import { useEffect, useRef, useState } from "react";
import { useAppStore } from "@/stores/app-store";
import { api } from "@/lib/api";
import { cn } from "@/lib/utils";

// SearchBar with debounce + autocomplete suggestions.
//
// Modernized UI: command-palette-style centered search, with a small
// "Extract pending" button to the right when there's backlog.
export function SearchBar() {
  const { query, setQuery, runSearch, searching, searchError, stats, extractPending } =
    useAppStore();
  const [local, setLocal] = useState(query);
  const [suggestions, setSuggestions] = useState<string[]>([]);
  const [showSug, setShowSug] = useState(false);
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const sugDebounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  // Sync local input if store query changes externally.
  useEffect(() => {
    setLocal(query);
  }, [query]);

  // Debounced search.
  useEffect(() => {
    if (debounceRef.current) clearTimeout(debounceRef.current);
    debounceRef.current = setTimeout(() => {
      if (local !== query) {
        setQuery(local);
        runSearch();
      }
    }, 250);
    return () => {
      if (debounceRef.current) clearTimeout(debounceRef.current);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [local]);

  // Debounced suggestions.
  useEffect(() => {
    if (sugDebounceRef.current) clearTimeout(sugDebounceRef.current);
    if (local.trim().length < 2) {
      setSuggestions([]);
      return;
    }
    sugDebounceRef.current = setTimeout(async () => {
      try {
        const s = await api.Suggestions(local.trim(), 8);
        setSuggestions(s);
      } catch {
        setSuggestions([]);
      }
    }, 120);
    return () => {
      if (sugDebounceRef.current) clearTimeout(sugDebounceRef.current);
    };
  }, [local]);

  const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === "Enter") {
      setQuery(local);
      runSearch();
      setShowSug(false);
    } else if (e.key === "Escape") {
      setLocal("");
      setQuery("");
      runSearch();
    }
  };

  const pending = stats?.pending ?? 0;

  return (
    <div className="relative flex items-center gap-2 border-b border-border bg-bg px-4 py-3">
      <div className="relative flex-1 max-w-3xl mx-auto">
        <Search
          size={16}
          className="pointer-events-none absolute left-3 top-1/2 -translate-y-1/2 text-fg-subtle"
        />
        <input
          type="text"
          value={local}
          onChange={(e) => setLocal(e.target.value)}
          onKeyDown={handleKeyDown}
          onFocus={() => setShowSug(true)}
          onBlur={() => setTimeout(() => setShowSug(false), 150)}
          placeholder="Search documents — use quotes for phrases, type:pdf folder:Railway date:>2024-01-01"
          className={cn(
            "w-full rounded-md border border-border bg-bg-subtle py-2 pl-9 pr-9 text-sm",
            "placeholder:text-fg-subtle focus:outline-none focus:ring-2 focus:ring-accent/50 focus:border-accent",
            "transition-colors"
          )}
        />
        {searching ? (
          <Loader2
            size={15}
            className="absolute right-3 top-1/2 -translate-y-1/2 animate-spin text-fg-subtle"
          />
        ) : local ? (
          <button
            onClick={() => {
              setLocal("");
              setQuery("");
              runSearch();
            }}
            className="absolute right-3 top-1/2 -translate-y-1/2 text-fg-subtle hover:text-fg"
          >
            <X size={15} />
          </button>
        ) : null}

        {/* Suggestions dropdown */}
        {showSug && suggestions.length > 0 && (
          <ul className="absolute z-10 mt-1 w-full rounded-md border border-border bg-bg-subtle py-1 shadow-lg animate-slide-up">
            {suggestions.map((s) => (
              <li key={s}>
                <button
                  className="flex w-full items-center gap-2 px-3 py-1.5 text-left text-xs hover:bg-bg-hover"
                  onClick={() => {
                    setLocal(s);
                    setQuery(s);
                    runSearch();
                    setShowSug(false);
                  }}
                >
                  <Search size={11} className="text-fg-subtle" />
                  <span className="truncate">{s}</span>
                </button>
              </li>
            ))}
          </ul>
        )}
      </div>

      {pending > 0 && (
        <button
          onClick={() => extractPending()}
          className="shrink-0 rounded-md border border-border bg-bg-subtle px-3 py-1.5 text-xs hover:bg-bg-hover"
          title="Extract content from pending files"
        >
          Extract {pending} pending
        </button>
      )}

      {searchError && (
        <div className="absolute left-4 right-4 top-full bg-red-950/80 border border-red-700 text-red-200 text-xs px-3 py-1.5 rounded-b-md">
          {searchError}
        </div>
      )}
    </div>
  );
}
