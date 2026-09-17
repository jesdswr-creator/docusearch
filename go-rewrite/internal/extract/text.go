package extract

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

// TextExtractor handles plain-text files.
//
// Supported extensions mirror the C++ app: .txt, .md, .csv, .rtf, .log.
// RTF gets a very light tag-stripping pass (good enough for indexing).
type TextExtractor struct{}

func NewTextExtractor() *TextExtractor { return &TextExtractor{} }

func (t *TextExtractor) Name() string { return "text" }

func (t *TextExtractor) Supports(ext string) bool {
	switch ext {
	case ".txt", ".md", ".csv", ".log", ".rtf":
		return true
	}
	return false
}

func (t *TextExtractor) Extract(ctx context.Context, path string) (*Result, error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open text file: %w", err)
	}
	defer f.Close()

	data, err := io.ReadAll(f)
	if err != nil {
		return nil, fmt.Errorf("read text file: %w", err)
	}

	text := string(data)
	if strings.ToLower(filepath.Ext(path)) == ".rtf" {
		text = stripRTF(text)
	}

	return &Result{
		Text:      strings.TrimSpace(text),
		PageCount: 0,
		Method:    "text/plain",
	}, nil
}

// stripRTF removes the most common RTF control words. Not a full RTF parser.
func stripRTF(s string) string {
	var b strings.Builder
	in := false
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c == '{' {
			in = true
			continue
		}
		if c == '}' {
			in = false
			continue
		}
		if c == '\\' {
			// Skip control word until non-letter.
			i++
			for i < len(s) && (s[i] >= 'a' && s[i] <= 'z' || s[i] >= 'A' && s[i] <= 'Z') {
				i++
			}
			// Optional numeric arg.
			if i < len(s) && (s[i] == '-' || (s[i] >= '0' && s[i] <= '9')) {
				i++
				for i < len(s) && s[i] >= '0' && s[i] <= '9' {
					i++
				}
			}
			// Trailing space is consumed by the control word.
			if i < len(s) && s[i] == ' ' {
				i++
			}
			i-- // compensate for outer loop's i++
			continue
		}
		_ = in
		b.WriteByte(c)
	}
	return b.String()
}
