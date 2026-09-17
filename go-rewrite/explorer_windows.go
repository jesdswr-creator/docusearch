//go:build windows

package main

import (
	"fmt"
	"os/exec"
	"path/filepath"
	"strings"
)

// revealInExplorerWindows opens Windows Explorer with the file selected.
//
// Implementation detail: explorer.exe's /select switch requires a normalized
// Windows path. We don't shell out to cmd.exe — just explorer directly.
func revealInExplorerWindows(path string) error {
	abs, err := filepath.Abs(path)
	if err != nil {
		return fmt.Errorf("abs path: %w", err)
	}
	// Replace forward slashes with backslashes just in case.
	abs = strings.ReplaceAll(abs, "/", "\\")

	cmd := exec.Command("explorer.exe", "/select,", abs)
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("start explorer: %w", err)
	}
	// Don't wait — explorer.exe stays open after we exit.
	return nil
}
