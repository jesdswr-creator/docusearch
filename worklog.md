---
Task ID: redesign-1
Agent: main
Task: Implement the user's HTML design completely bit by bit, compile it properly, fix Windows OCR.

Work Log:
- Read the user's HTML design (809 lines) at upload/Pasted Content_1782997045199.txt
- Catalogued design tokens: bg #f0f2f5, panels #fff, accent #2563eb, text #1a1a2e, secondary #6b7280, border #e5e7eb, radius 8px
- Catalogued layout: 44px title bar (logo + title + theme/min/max/close) + 200px sidebar (9 nav items + indexed status) + search bar (input + Search btn + Saved + Filters + Add Folder + 4 icon btns) + 3-way splitter (results 340px / viewer flex / metadata 300px with metadata top + tags/notes bottom) + 32px status bar (dot + Ready + counts + Open Location btn)
- Downloaded 49 Lucide SVG icons (filter, refresh-cw, list, layout-grid, more-horizontal, chevron-left/right, maximize-2, rotate-cw, copy, download, file, folder, upload, calendar, lock, image, pencil, trash-2, save, bookmark, plus-circle, alert-circle, play, pause, eye, minus, square, etc.)
- Updated resources/app.qrc to include all 49 icons
- Created src/ui/IconUtils.h with loadLucideIcon() and loadLucidePixmap() helpers (palette-aware SVG rendering via currentColor replacement)
- Rewrote src/ui/Theme.cpp — complete QSS rewrite for light + dark modes matching the design's exact color palette and object names (titleBar, sidebar, searchBarArea, searchInput, searchBtn, resultsPanel, viewerPanel, metadataPanel, extractedPanel, indexedStatus, statusDot, etc.)
- Rewrote src/ui/SearchBar.h/.cpp — custom SearchLineEdit with leading search icon + overlaid clear button + Search button with icon + "Search" text + "Ctrl+K" badge + Saved Searches dropdown + Filters button + Add Folder button + 4 icon buttons (Refresh, List, Grid, More)
- Rewrote src/ui/ResultsPane.h/.cpp — header (title + count + sort dropdown) + QListWidget of custom item widgets (file-type colored badge 36x36 + title + snippet + meta + active dot)
- Rewrote src/ui/PreviewPane.h/.cpp — viewer header (filename + page nav ‹ [1] /2 › + zoom − 100% + + actions fit/rotate/more) + document page QTextEdit + extracted panel at bottom (tabs Extracted/AI Summary/Highlights/Related + content + Copy/Download actions)
- Rewrote src/ui/MetadataPane.h/.cpp — header (Metadata + info icon + edit button) + 9 metadata rows each with icon (file, folder, file, upload, calendar, calendar, lock, search, image) + label + value
- Rewrote src/ui/TagsNotesPane.h/.cpp — Tags section (header + tag icon + flow layout of colored pills tagBlue/tagGreen/tagYellow/tagPurple + Add Tag button + hidden input) + Notes section (header + notes icon + QTextEdit + modified label)
- Rewrote src/ui/MainWindow.h/.cpp — frameless window with custom TitleBarWidget (mouse drag to move + double-click to maximize) + nativeEvent handler for WM_NCHITTEST edge resizing + buildTitleBar (logo + title + theme/min/max/close) + buildCentral (sidebar 200px + center panel with search bar + 3-way splitter) + buildStatusBar (dot + Ready + counts + Open Location button) + applyTheme (calls Theme::apply + refreshAllIcons) + refreshAllIcons (re-renders all icons after theme toggle) + sidebar click handler (Settings/About/Help/Stats/Saved/Tags/Notes/Recent) + onOpenLocation + onRefresh + onFilters
- Removed menu bar entirely (no QMenuBar)

Stage Summary:
- All UI files rewritten to match the user's HTML design exactly
- Custom title bar replaces native window frame
- All widgets use objectNames so Theme QSS handles styling (no inline stylesheets except for a few unavoidable overlays like the clear button's gray circle and tag pill flow layout)
- Icons are palette-aware (re-rendered on theme toggle via refreshAllIcons)
- Build has NOT been attempted yet — next step is to verify it compiles, then implement Windows OCR via raw COM ABI (no WinRT headers, no runtimeobject.lib auto-link conflict)

Next steps:
- Attempt local Linux build to catch syntax errors (won't link Qt6/Poppler but will catch C++ errors)
- Implement WindowsOcrEngine.cpp using raw COM ABI (LoadLibrary combase.dll + GetProcAddress for RoInitialize, RoGetActivationFactory, Windows{.Create,*.CreateOver_inspectable} — manually declared IIDs, no winrt/ headers)
- Update CMakeLists.txt to enable DOCUSEARCH_HAS_WINDOWS_OCR
- Commit + push to GitHub to trigger Windows CI build
