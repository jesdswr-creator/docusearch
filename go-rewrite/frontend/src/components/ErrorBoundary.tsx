import { Component, type ErrorInfo, type ReactNode } from "react";

interface Props {
  children: ReactNode;
}
interface State {
  hasError: boolean;
  error: Error | null;
}

// ErrorBoundary catches render-time exceptions and shows a visible
// error screen instead of an unmounted (blank) tree.
//
// Wails v2 + React without an error boundary = "blank page" UX on any
// uncaught render error. This component makes those errors visible so
// they can be diagnosed.
export class ErrorBoundary extends Component<Props, State> {
  constructor(props: Props) {
    super(props);
    this.state = { hasError: false, error: null };
  }

  static getDerivedStateFromError(error: Error): State {
    return { hasError: true, error };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    // eslint-disable-next-line no-console
    console.error("ErrorBoundary caught:", error, info);
  }

  render() {
    if (this.state.hasError) {
      return (
        <div
          style={{
            padding: "32px",
            fontFamily: "monospace",
            background: "#1a0000",
            color: "#ff8080",
            minHeight: "100vh",
            boxSizing: "border-box",
            whiteSpace: "pre-wrap",
            overflow: "auto",
          }}
        >
          <h1 style={{ color: "#ff4040", marginBottom: "16px" }}>
            DocuSearch crashed
          </h1>
          <p style={{ marginBottom: "16px", color: "#ffa0a0" }}>
            An uncaught error during render. Details below — press
            Ctrl+Shift+I to open dev tools for full console output.
          </p>
          <pre style={{ fontSize: "12px", lineHeight: 1.5 }}>
            {this.state.error?.message ?? "(no message)"}
            {"\n\n"}
            {this.state.error?.stack ?? "(no stack)"}
          </pre>
          <button
            onClick={() => this.setState({ hasError: false, error: null })}
            style={{
              marginTop: "16px",
              padding: "8px 16px",
              background: "#ff4040",
              color: "#000",
              border: "none",
              cursor: "pointer",
              fontFamily: "monospace",
            }}
          >
            Try again
          </button>
        </div>
      );
    }
    return this.props.children;
  }
}
