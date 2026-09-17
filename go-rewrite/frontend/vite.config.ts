import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import path from "path";

// Wails serves the built assets from `frontend/dist`. In dev mode Wails
// proxies to the Vite dev server (configured via wails.json).
export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./src"),
    },
  },
  server: {
    port: 5173,
    strictPort: true,
    host: "127.0.0.1",
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2022",
    sourcemap: false,
  },
});
