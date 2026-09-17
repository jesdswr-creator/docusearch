package extract

import (
	"context"
	"fmt"
	"strings"

	"github.com/ledongthuc/pdf"
)

// PDFExtractor extracts text from born-digital PDFs.
//
// PoC: uses github.com/ledongthuc/pdf (pure Go, no CGO).
// Production target: swap to PDFium via CGO bindings for parity with the
// C++ app (handles encrypted PDFs, better rendering, image-only PDFs via
// the OCR pipeline). The interface stays the same.
type PDFExtractor struct{}

func NewPDFExtractor() *PDFExtractor { return &PDFExtractor{} }

func (p *PDFExtractor) Name() string         { return "pdf" }
func (p *PDFExtractor) Supports(ext string) bool { return ext == ".pdf" }

func (p *PDFExtractor) Extract(ctx context.Context, path string) (*Result, error) {
	// Check context before slow operations.
	if err := ctx.Err(); err != nil {
		return nil, err
	}

	f, r, err := pdf.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open pdf: %w", err)
	}
	defer func() { _ = f.Close() }()

	var b strings.Builder
	totalPages := r.NumPage()
	for i := 1; i <= totalPages; i++ {
		if err := ctx.Err(); err != nil {
			return nil, err
		}
		page := r.Page(i)
		if page.V.IsNull() {
			continue
		}
		text, err := page.GetPlainText(nil)
		if err != nil {
			// Continue with what we have — partial extraction is better than none.
			continue
		}
		b.WriteString(text)
		b.WriteString("\n\n")
	}

	return &Result{
		Text:      strings.TrimSpace(b.String()),
		PageCount: totalPages,
		Method:    "ledongthuc/pdf",
	}, nil
}
