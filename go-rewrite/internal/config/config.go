package config

import (
	"log/slog"
	"os"
	"path/filepath"
)

// Config holds runtime configuration. The PoC derives most values from
// environment variables or sensible defaults; a full SettingsDialog
// parity will be added later (mirrors the C++ app's SettingsManager).
type Config struct {
	// DBPath is the location of the SQLite database file.
	DBPath string
	// LogLevel: debug | info | warn | error
	LogLevel string
	// LogFile: where to write logs. Empty = stderr.
	LogFile string
	// MaxConcurrentExtractions: future, when we add a worker pool.
	MaxConcurrentExtractions int
}

// Default returns the default config for the current platform.
//
// On Windows the data dir is %APPDATA%\DocuSearch (matches the C++ app).
func Default() Config {
	appdata := os.Getenv("APPDATA")
	var dataDir string
	if appdata != "" {
		dataDir = filepath.Join(appdata, "DocuSearch")
	} else {
		// Non-Windows dev/test fallback.
		home, _ := os.UserHomeDir()
		dataDir = filepath.Join(home, ".docusearch")
	}
	return Config{
		DBPath:                  filepath.Join(dataDir, "docusearch.db"),
		LogLevel:                "info",
		LogFile:                 filepath.Join(dataDir, "logs", "docusearch.log"),
		MaxConcurrentExtractions: 4,
	}
}

// Logger builds a structured logger from the config.
func (c Config) Logger() *slog.Logger {
	level := slog.LevelInfo
	switch c.LogLevel {
	case "debug":
		level = slog.LevelDebug
	case "warn":
		level = slog.LevelWarn
	case "error":
		level = slog.LevelError
	}

	if c.LogFile != "" {
		if err := os.MkdirAll(filepath.Dir(c.LogFile), 0o755); err == nil {
			f, err := os.OpenFile(c.LogFile, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
			if err == nil {
				return slog.New(slog.NewTextHandler(f, &slog.HandlerOptions{Level: level}))
			}
		}
	}
	return slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: level}))
}
