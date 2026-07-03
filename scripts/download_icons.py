#!/usr/bin/env python3
"""Download all Lucide SVG icons needed for the DocuSearch UI design."""
import os
import urllib.request
import sys

OUT = "/home/z/my-project/resources/icons/lucide"
os.makedirs(OUT, exist_ok=True)

# All icons we need for the design. Some already exist; we'll re-download them
# to make sure they're all consistent and current.
icons = [
    # Title bar
    "sun", "moon", "minus", "square", "x",
    # Sidebar nav
    "home", "bookmark", "tag", "sticky-note", "bar-chart-3", "clock",
    "settings", "help-circle", "info",
    # Search bar
    "search", "chevron-down", "filter", "folder-plus", "refresh-cw",
    "list", "layout-grid", "more-horizontal", "folder",
    # Viewer panel
    "chevron-left", "chevron-right", "plus", "maximize-2", "rotate-cw",
    "copy", "download", "file-text",
    # Metadata
    "file", "upload", "calendar", "lock", "image", "pencil",
    # Tags / Notes
    "trash-2", "save",
    # Misc
    "check-circle", "database", "duplicate", "arrow-left", "arrow-right",
    "plus-circle", "alert-circle", "play", "pause", "eye",
]

base = "https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/{}.svg"
ok = []
fail = []
for name in icons:
    url = base.format(name)
    path = os.path.join(OUT, f"{name}.svg")
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=15) as r:
            data = r.read()
        with open(path, "wb") as f:
            f.write(data)
        ok.append(name)
        print(f"  OK: {name} ({len(data)} bytes)")
    except Exception as e:
        fail.append((name, str(e)))
        print(f"  FAIL: {name} - {e}")

print(f"\n=== Summary ===")
print(f"Downloaded: {len(ok)} icons")
print(f"Failed: {len(fail)} icons")
if fail:
    print("Failed list:")
    for n, e in fail:
        print(f"  - {n}: {e}")
