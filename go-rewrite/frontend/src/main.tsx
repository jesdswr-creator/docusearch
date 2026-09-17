import React from "react";
import ReactDOM from "react-dom/client";
import App from "./App";
import { ErrorBoundary } from "@/components/ErrorBoundary";
import "./styles/globals.css";

// Startup log — visible in WebView2 dev tools (Ctrl+Shift+I).
// Helps diagnose "blank page" issues by confirming whether the JS
// bundle is even executing.
// eslint-disable-next-line no-console
console.log("[DocuSearch] frontend bundle loaded at", new Date().toISOString());

ReactDOM.createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <ErrorBoundary>
      <App />
    </ErrorBoundary>
  </React.StrictMode>
);
