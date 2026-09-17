// Package main is the Wails entrypoint for DocuSearch Go rewrite.
package main

import (
	"embed"
	"log/slog"

	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/config"
	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/db"
	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/indexer"
	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/ocr"
	"github.com/jesdswr-creator/docusearch/go-rewrite/internal/search"

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

	database, err := db.Open(cfg.DBPath)
	if err != nil {
		logger.Error("open database", "err", err, "path", cfg.DBPath)
		panic(err)
	}
	defer database.Close()

	pipe := indexer.NewPipeline(database, logger)
	eng := search.NewEngine(database)
	ocrEng := ocr.New()

	app := &App{
		cfg:      cfg,
		db:       database,
		pipeline: pipe,
		search:   eng,
		ocr:      ocrEng,
		logger:   logger,
	}

	err = wails.Run(&options.App{
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
