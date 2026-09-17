//go:build !windows

package main

// revealInExplorerWindows is a no-op on non-Windows for dev builds.
func revealInExplorerWindows(path string) error {
	return nil
}
