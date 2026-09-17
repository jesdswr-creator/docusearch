package extract

import (
	"archive/zip"
	"bytes"
	"context"
	"fmt"
	"io"
	"strings"
)

// XLSXExtractor handles .xlsx files.
//
// .xlsx is a ZIP containing xl/worksheets/sheet*.xml and a shared
// strings table at xl/sharedStrings.xml. Cell values reference the
// shared strings table by index when type="s".
type XLSXExtractor struct{}

func NewXlsxExtractor() *XLSXExtractor { return &XLSXExtractor{} }

func (x *XLSXExtractor) Name() string             { return "xlsx" }
func (x *XLSXExtractor) Supports(ext string) bool { return ext == ".xlsx" || ext == ".xlsm" }

func (x *XLSXExtractor) Extract(ctx context.Context, path string) (*Result, error) {
	zr, err := zip.OpenReader(path)
	if err != nil {
		return nil, fmt.Errorf("open xlsx (zip): %w", err)
	}
	defer zr.Close()

	sharedStrings, err := readSharedStrings(zr)
	if err != nil {
		return nil, err
	}

	var b strings.Builder
	for _, f := range zr.File {
		if err := ctx.Err(); err != nil {
			return nil, err
		}
		if !strings.HasPrefix(f.Name, "xl/worksheets/sheet") || !strings.HasSuffix(f.Name, ".xml") {
			continue
		}
		rc, err := f.Open()
		if err != nil {
			continue
		}
		data, _ := io.ReadAll(rc)
		_ = rc.Close()

		for _, v := range extractCellValues(data, sharedStrings) {
			b.WriteString(v)
			b.WriteString(" | ")
		}
		b.WriteByte('\n')
	}

	return &Result{
		Text:      strings.TrimSpace(b.String()),
		PageCount: 0,
		Method:    "xlsx/xml",
	}, nil
}

func readSharedStrings(zr *zip.ReadCloser) ([]string, error) {
	f, err := zr.Open("xl/sharedStrings.xml")
	if err != nil {
		// Some XLSX files (e.g. ones without shared strings) don't have this file.
		return nil, nil
	}
	defer f.Close()
	data, err := io.ReadAll(f)
	if err != nil {
		return nil, fmt.Errorf("read sharedStrings: %w", err)
	}

	// Naive parse: pull out <si><t>...</t></si> blocks.
	var out []string
	si := []byte("<si>")
	siEnd := []byte("</si>")
	tStart := []byte("<t")
	tEnd := []byte("</t>")
	selfClose := []byte("/>")

	for {
		i := bytes.Index(data, si)
		if i < 0 {
			break
		}
		j := bytes.Index(data[i:], siEnd)
		if j < 0 {
			break
		}
		block := data[i : i+j+len(siEnd)]
		// Find <t>...</t> within block.
		ti := bytes.Index(block, tStart)
		if ti >= 0 {
			gt := bytes.IndexByte(block[ti:], '>')
			if gt >= 0 && !bytes.HasSuffix(block[ti:ti+gt+1], selfClose) {
				te := bytes.Index(block[ti+gt+1:], tEnd)
				if te >= 0 {
					out = append(out, decodeXMLEntities(string(block[ti+gt+1:ti+gt+1+te])))
				}
			}
		}
		data = data[i+j+len(siEnd):]
	}
	return out, nil
}

func extractCellValues(sheetXML []byte, sharedStrings []string) []string {
	var out []string
	vStart := []byte("<v>")
	vEnd := []byte("</v>")
	// Walk cells: when <c t="s"> then the <v> contains an index into sharedStrings.
	cStart := []byte("<c ")
	cEnd := []byte("</c>")
	for {
		i := bytes.Index(sheetXML, cStart)
		if i < 0 {
			break
		}
		j := bytes.Index(sheetXML[i:], cEnd)
		if j < 0 {
			// Cell may be self-closing <c .../>.
			selfEnd := bytes.Index(sheetXML[i:], []byte("/>"))
			if selfEnd < 0 {
				break
			}
			sheetXML = sheetXML[i+selfEnd+2:]
			continue
		}
		cellBlock := sheetXML[i : i+j+len(cEnd)]
		// Determine type.
		isShared := bytes.Contains(cellBlock, []byte(`t="s"`))
		// Pull <v>...</v>.
		vi := bytes.Index(cellBlock, vStart)
		if vi >= 0 {
			vj := bytes.Index(cellBlock[vi:], vEnd)
			if vj >= 0 {
				raw := string(cellBlock[vi+len(vStart) : vi+vj])
				if isShared {
					// raw is an index into sharedStrings.
					var idx int
					_, err := fmt.Sscanf(raw, "%d", &idx)
					if err == nil && idx >= 0 && idx < len(sharedStrings) {
						out = append(out, sharedStrings[idx])
					}
				} else {
					out = append(out, decodeXMLEntities(raw))
				}
			}
		}
		sheetXML = sheetXML[i+j+len(cEnd):]
	}
	return out
}

func decodeXMLEntities(s string) string {
	s = strings.ReplaceAll(s, "&amp;", "&")
	s = strings.ReplaceAll(s, "&lt;", "<")
	s = strings.ReplaceAll(s, "&gt;", ">")
	s = strings.ReplaceAll(s, "&quot;", `"`)
	s = strings.ReplaceAll(s, "&apos;", "'")
	return s
}
