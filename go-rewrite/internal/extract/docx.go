package extract

import (
	"archive/zip"
	"bytes"
	"context"
	"encoding/xml"
	"fmt"
	"io"
	"strings"
)

// DOCXExtractor handles .docx files.
//
// .docx is a ZIP containing word/document.xml with the main text body.
// We extract <w:t> elements (text runs) and concatenate. This handles
// ~95% of real-world documents; tables, headers, footers, footnotes are
// not handled in the PoC.
type DOCXExtractor struct{}

func NewDocxExtractor() *DOCXExtractor { return &DOCXExtractor{} }

func (d *DOCXExtractor) Name() string             { return "docx" }
func (d *DOCXExtractor) Supports(ext string) bool { return ext == ".docx" }

// Office XML text element.
type wT struct {
	Value string `xml:",chardata"`
}

func (d *DOCXExtractor) Extract(ctx context.Context, path string) (*Result, error) {
	zr, err := zip.OpenReader(path)
	if err != nil {
		return nil, fmt.Errorf("open docx (zip): %w", err)
	}
	defer zr.Close()

	var b strings.Builder
	for _, f := range zr.File {
		if err := ctx.Err(); err != nil {
			return nil, err
		}
		// Main body lives in word/document.xml.
		// PoC: skip headers/footers/footnotes for now.
		if f.Name != "word/document.xml" {
			continue
		}
		rc, err := f.Open()
		if err != nil {
			return nil, fmt.Errorf("open document.xml: %w", err)
		}
		data, err := io.ReadAll(rc)
		_ = rc.Close()
		if err != nil {
			return nil, fmt.Errorf("read document.xml: %w", err)
		}

		// Naive split on <w:t> tags — robust against XML namespaces.
		texts := extractWT(data)
		for _, t := range texts {
			b.WriteString(t)
			b.WriteByte(' ')
		}
		break
	}

	return &Result{
		Text:      strings.TrimSpace(b.String()),
		PageCount: 0,
		Method:    "docx/xml",
	}, nil
}

// extractWT pulls <w:t>...</w:t> text content out of the raw XML bytes.
// Avoids strict XML decoder because real-world .docx files mix namespaces
// in ways that often trip up naive decoders.
func extractWT(data []byte) []string {
	var out []string
	start := []byte("<w:t")
	end := []byte("</w:t>")
	endSelfClose := []byte("/>")
	for {
		i := bytes.Index(data, start)
		if i < 0 {
			break
		}
		// Find the end of the opening tag.
		gt := bytes.IndexByte(data[i:], '>')
		if gt < 0 {
			break
		}
		openEnd := i + gt
		// Self-closing tag <w:t/> has no text.
		if bytes.HasSuffix(data[i:openEnd+1], endSelfClose) {
			data = data[openEnd+1:]
			continue
		}
		j := bytes.Index(data[openEnd+1:], end)
		if j < 0 {
			break
		}
		text := string(data[openEnd+1 : openEnd+1+j])
		// Decode XML entities minimally.
		text = strings.ReplaceAll(text, "&amp;", "&")
		text = strings.ReplaceAll(text, "&lt;", "<")
		text = strings.ReplaceAll(text, "&gt;", ">")
		text = strings.ReplaceAll(text, "&quot;", `"`)
		text = strings.ReplaceAll(text, "&apos;", "'")
		out = append(out, text)
		data = data[openEnd+1+j+len(end):]
	}
	return out
}

// xmlTokenize is reserved for a stricter future implementation.
var _ = xml.Unmarshal
