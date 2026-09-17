// Package extract provides document text extractors for various file types.
package extract

import (
	"context"
	"fmt"
	"strings"
)

// Result is the output of an extraction.
type Result struct {
	Text      string
	PageCount int
	// Method indicates which extractor was used (for debugging / telemetry).
	Method string
}

// Extractor is the interface every file-type extractor implements.
type Extractor interface {
	// Supports returns true if this extractor can handle the given extension.
	// The extension includes the leading dot, lowercased — e.g. ".pdf".
	Supports(ext string) bool
	// Extract reads the file at path and returns extracted text.
	Extract(ctx context.Context, path string) (*Result, error)
	// Name returns a human-friendly name for the extractor.
	Name() string
}

// Registry routes an extension to the right extractor.
type Registry struct {
	extractors []Extractor
}

// NewRegistry builds a Registry with all registered extractors.
// Order matters — first extractor that Supports() a given extension wins.
func NewRegistry() *Registry {
	return &Registry{
		extractors: []Extractor{
			NewPDFExtractor(),
			NewDocxExtractor(),
			NewXlsxExtractor(),
			NewPptxExtractor(),
			NewTextExtractor(),
		},
	}
}

// For returns the extractor for the given extension, or nil if none match.
func (r *Registry) For(ext string) Extractor {
	ext = strings.ToLower(ext)
	if !strings.HasPrefix(ext, ".") {
		ext = "." + ext
	}
	for _, e := range r.extractors {
		if e.Supports(ext) {
			return e
		}
	}
	return nil
}

// IsIndexable returns true if any registered extractor handles this extension.
func (r *Registry) IsIndexable(ext string) bool {
	return r.For(ext) != nil
}

// Extract dispatches to the right extractor and returns its result.
func (r *Registry) Extract(ctx context.Context, path, ext string) (*Result, error) {
	e := r.For(ext)
	if e == nil {
		return nil, fmt.Errorf("no extractor for extension %q", ext)
	}
	return e.Extract(ctx, path)
}
