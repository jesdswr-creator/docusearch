# Third-Party Notices — DocuSearch

DocuSearch is © 2024–2026 DocuSearch, licensed under the BSD 3-Clause
License (see `LICENSE`). It incorporates or links against the following
third-party components. Each component remains under its own license;
nothing in DocuSearch's license modifies your rights under them.

---

## Qt 6 (Qt Toolkit)
* **Website:** https://www.qt.io
* **License:** GNU Lesser General Public License v3 (LGPLv3)
* **Usage:** dynamically linked (Qt6Core, Qt6Gui, Qt6Widgets, Qt6Concurrent,
  Qt6Sql, Qt6Network, Qt6Svg, Qt6PrintSupport DLLs shipped alongside
  `DocuSearch.exe`).

This product uses the Qt Toolkit under the terms of the LGPLv3. The full
license text is available at https://www.gnu.org/licenses/lgpl-3.0.html
and the corresponding Qt source code at https://code.qt.io.

**Your LGPL rights:** because Qt is dynamically linked, you may replace the
Qt libraries shipped with DocuSearch with your own (compatible) builds. The
portable ZIP distribution of DocuSearch places the Qt DLLs next to the
executable precisely so they can be swapped. Nothing in DocuSearch's EULA
restricts modification of, or reverse engineering for debugging
modifications to, the Qt libraries themselves.

## PDFium
* **Website:** https://pdfium.googlesource.com/pdfium/
* **Binaries:** https://github.com/bblanchon/pdfium-binaries
* **License:** Apache License 2.0 / BSD-3-Clause-style (see
  `docs/PDFIUM_LICENSE.txt` in the distribution)
* **Usage:** PDF rendering and text extraction (`pdfium.dll`).

## SQLite
* **Website:** https://sqlite.org
* **License:** Public domain
* **Usage:** index database (FTS5 full-text search), statically compiled.

## ONNX Runtime (optional component)
* **Website:** https://onnxruntime.ai
* **License:** MIT License
* **Usage:** neural-network inference for semantic search
  (`onnxruntime.dll`, shipped when semantic search is bundled).

## BGE-small-en-v1.5 embedding model (optional component)
* **Source:** https://huggingface.co/BAAI/bge-small-en-v1.5
* **License:** MIT License (© Beijing Academy of Artificial Intelligence)
* **Usage:** text-embedding model powering semantic search (`model.onnx`).

## Lucide Icons
* **Website:** https://lucide.dev
* **License:** ISC License (© Lucide Contributors, portions © 2013–2022
  Cole Bemis / Feather)
* **Usage:** user-interface icons, compiled into the executable.

## zlib / Minizip
* **Website:** https://zlib.net
* **License:** zlib License (© Jean-loup Gailly and Mark Adler)
* **Usage:** reading OOXML (`.docx`/`.xlsx`/`.pptx`) archives.

## Windows.Media.Ocr
* **Vendor:** Microsoft (component of Windows 10/11)
* **Usage:** optical character recognition via the public WinRT API.
  Nothing is redistributed — DocuSearch calls the OCR engine already
  present in the user's operating system. See `docs/OCR_LICENSING.md`.

---

### License texts

Full license texts for the components above:

* LGPLv3 — https://www.gnu.org/licenses/lgpl-3.0.html
* Apache 2.0 — https://www.apache.org/licenses/LICENSE-2.0
* MIT — https://opensource.org/license/mit
* ISC — https://opensource.org/license/isc-license-txt
* zlib — https://zlib.net/zlib_license.html

The PDFium license file is bundled as `docs/PDFIUM_LICENSE.txt` in binary
distributions.
