import { Sidebar } from "@/components/Sidebar";
import { SearchBar } from "@/components/SearchBar";
import { ResultsList } from "@/components/ResultsList";
import { PreviewPane } from "@/components/PreviewPane";

export default function App() {
  return (
    <div className="flex h-screen w-screen overflow-hidden bg-bg text-fg">
      {/* Left rail: folders + stats */}
      <Sidebar />

      {/* Main column: search bar + results list */}
      <main className="flex min-w-0 flex-1 flex-col">
        <SearchBar />
        <div className="min-h-0 flex-1">
          <ResultsList />
        </div>
      </main>

      {/* Right rail: preview + metadata */}
      <section className="flex min-w-0 w-[480px] flex-col border-l border-border bg-bg-subtle">
        <PreviewPane />
      </section>
    </div>
  );
}
