// Package ocr will host the Windows.Media.Ocr (WinRT) integration.
//
// The C++ app uses WinRT directly via C++/WinRT. For Go we have a few
// options when we get there:
//
//   1. Call WinRT from Go via the `go-ole` COM bindings — workable but
//      verbose. WinRT is COM-like, so the basic pattern (CoCreateInstance,
//      QueryInterface, async activation) maps onto go-ole.
//
//   2. Build a small C bridge (CGO) that exposes a clean
//      `ocr_image(path) -> text` API and let the C side use C++/WinRT
//      via a /clr or /ZW compile step. Higher build complexity but the
//      simplest Go side.
//
//   3. Use the `github.com/0xJackal/winrt-go` community bindings if they
//      reach a stable state by the time we need this.
//
// For the PoC this package is a no-op stub. The indexer already preserves
// the `ocr_text` column, so adding OCR later is a strict addition.
package ocr

import (
	"context"
	"errors"
)

// Engine is the interface that any OCR backend implements.
type Engine interface {
	// Available reports whether the OCR backend is usable on this machine
	// (e.g. WinRT is Windows-only and requires at least one language pack).
	Available() bool
	// SupportedLanguages lists installed OCR languages.
	SupportedLanguages() ([]string, error)
	// ExtractFromImage runs OCR on an image file and returns the text.
	ExtractFromImage(ctx context.Context, path string) (string, error)
}

// NoopEngine is the default OCR engine for the PoC. It always reports
// unavailable and returns ErrNotImplemented.
type NoopEngine struct{}

// ErrNotImplemented is returned by NoopEngine operations.
var ErrNotImplemented = errors.New("ocr not implemented in PoC build")

// New returns the best available OCR engine for the current platform.
// On Windows with the production build, this will return a *WinRTEngine.
// For the PoC, always returns NoopEngine.
func New() Engine { return &NoopEngine{} }

func (n *NoopEngine) Available() bool                  { return false }
func (n *NoopEngine) SupportedLanguages() ([]string, error) {
	return nil, ErrNotImplemented
}
func (n *NoopEngine) ExtractFromImage(ctx context.Context, path string) (string, error) {
	return "", ErrNotImplemented
}
