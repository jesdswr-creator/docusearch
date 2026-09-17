package extract

import (
	"archive/zip"
	"bytes"
	"context"
	"fmt"
	"io"
	"strings"
)

// PPTXExtractor handles .pptx files.
//
// .pptx is a ZIP containing ppt/slides/slide*.xml files.
// We extract <a:t> elements (text runs) from each slide.
type PPTXExtractor struct{}

func NewPptxExtractor() *PPTXExtractor { return &PPTXExtractor{} }

func (p *PPTXExtractor) Name() string             { return "pptx" }
func (p *PPTXExtractor) Supports(ext string) bool { return ext == ".pptx" }

func (p *PPTXExtractor) Extract(ctx context.Context, path string) (*Result, error) {
	zr, err := zip.OpenReader(path)
	if err != nil {
		return nil, fmt.Errorf("open pptx (zip): %w", err)
	}
	defer zr.Close()

	var b strings.Builder
	pageCount := 0
	for _, f := range zr.File {
		if err := ctx.Err(); err != nil {
			return nil, err
		}
		if !strings.HasPrefix(f.Name, "ppt/slides/slide") || !strings.HasSuffix(f.Name, ".xml") {
			continue
		}
		pageCount++
		rc, err := f.Open()
		if err != nil {
			continue
		}
		data, _ := io.ReadAll(rc)
		_ = rc.Close()

		for _, t := range extractAT(data) {
			b.WriteString(t)
			b.WriteByte(' ')
		}
		b.WriteString("\n\n---\n\n")
	}

	return &Result{
		Text:      strings.TrimSpace(b.String()),
		PageCount: pageCount,
		Method:    "pptx/xml",
	}, nil
}

// extractAT pulls <a:t>...</a:t> text content out of the raw XML bytes.
func extractAT(data []byte) []string {
	var out []string
	start := []byte("<a:t>")
	end := []byte("</a:t>")
	for {
		i := bytes.Index(data, start)
		if i < 0 {
			break
		}
		j := bytes.Index(data[i:], end)
		if j < 0 {
			break
		}
		text := string(data[i+len(start) : i+j])
		out = append(out, decodeXMLEntities(text))
		data = data[i+j+len(end):]
	}
	return out
}
