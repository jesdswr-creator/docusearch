package main

// revealInExplorer opens Windows Explorer with the given file selected.
//
// On non-Windows (dev builds) it's a no-op that returns nil.
//
// We use a separate file (not syscall directly in app.go) so the Windows
// build can use the proper Windows API without polluting the cross-platform
// file with build tags.
func revealInExplorer(path string) error {
	return revealInExplorerWindows(path)
}
