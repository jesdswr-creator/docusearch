// Package main is the Wails entrypoint for DocuSearch Go rewrite.
//
// IMPORTANT (Wails gotcha): main() must NOT do any work that can fail
// (DB connections, network, file IO). Wails runs main() during binding
// generation, and any panic there breaks the build. All real init goes
// in App.OnStartup instead.
package main

import (
	"embed"
	"log/slog"

	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/config"

	"github.com/wailsapp/wails/v2"
	"github.com/wailsapp/wails/v2/pkg/options"
	"github.com/wailsapp/wails/v2/pkg/options/assetserver"
	"github.com/wailsapp/wails/v2/pkg/options/windows"
)

//go:embed all:frontend/dist
var assets embed.FS

func main() {
	cfg := config.Default()
	logger := cfg.Logger()
	slog.SetDefault(logger)

	// App holds lazy-initialized subsystems. They are nil here and get
	// wired up in OnStartup. This keeps main() panic-free, which Wails
	// requires for binding generation.
	app := &App{
		cfg:    cfg,
		logger: logger,
	}

	err := wails.Run(&options.App{
		Title:     "DocuSearch",
		Width:     1280,
		Height:    800,
		MinWidth:  900,
		MinHeight: 600,
		AssetServer: &assetserver.Options{
			Assets: assets,
		},
		BackgroundColour: &options.RGBA{R: 24, G: 24, B: 27, A: 1},
		OnStartup:        app.OnStartup,
		OnShutdown:       app.OnShutdown,
		Bind: []interface{}{
			app,
		},
		Windows: &windows.Options{
			WebviewIsTransparent: false,
			WindowIsTranslucent:  false,
			Theme:                windows.Dark,
		},
	})
	if err != nil {
		logger.Error("wails run", "err", err)
		panic(err)
	}
}
