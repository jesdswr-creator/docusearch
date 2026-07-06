#!/usr/bin/env python3
"""Split Theme.cpp's raw string literals into multiple concatenated chunks.

MSVC has a limit on string literal size (C2026: string too big). Even
though our QSS strings are ~18000 chars (under the 65535 byte limit),
MSVC's raw string processing can hit an internal token limit.

The fix is to split each R"(...)" raw string into multiple smaller
R"(...)" raw strings separated by nothing (C++ concatenates adjacent
string literals automatically).

We split at section boundaries (lines starting with "/* =====").
"""
import re

content = open('/home/z/my-project/src/ui/Theme.cpp', 'r', encoding='utf-8').read()

# Find each raw string literal
def split_raw_string(text, start_marker):
    """Find R"( ... )" and split it into multiple R"( ... )" chunks."""
    # Find the start
    idx = text.find(start_marker)
    if idx == -1:
        return text
    # Find the opening R"(
    rstart = text.find('R"(', idx)
    if rstart == -1:
        return text
    # Find the closing )"
    rend = text.find(')"', rstart + 3)
    if rend == -1:
        return text
    
    prefix = text[:rstart + 3]  # everything up to and including R"(
    body = text[rstart + 3:rend]  # the QSS content
    suffix = text[rend:]  # )"; and everything after
    
    # Split the body at section boundaries (lines starting with /* =====)
    lines = body.split('\n')
    chunks = []
    current_chunk = []
    
    for line in lines:
        if line.strip().startswith('/* =====') and current_chunk:
            # Start a new chunk — close the current one and start a new one
            chunks.append('\n'.join(current_chunk))
            current_chunk = [line]
        else:
            current_chunk.append(line)
    if current_chunk:
        chunks.append('\n'.join(current_chunk))
    
    # Rebuild as multiple concatenated raw strings: R"(chunk1)" R"(chunk2)" ...
    # C++ automatically concatenates adjacent string literals.
    parts = []
    for chunk in chunks:
        # Skip empty chunks
        if chunk.strip():
            parts.append('R"(' + chunk + ')"')
    
    new_text = prefix[:-3]  # remove the R"( we already added
    # Actually, prefix ends with R"( — we want to replace R"(body)" with
    # R"(chunk1)" R"(chunk2)" ...
    new_text = text[:rstart] + ' '.join(parts) + suffix
    return new_text

# Apply to both kLightQss and kDarkQss
content = split_raw_string(content, 'const char* kLightQss')
content = split_raw_string(content, 'const char* kDarkQss')

# Also replace the em-dash characters (U+2014) with ASCII hyphens
# to avoid any encoding issues
content = content.replace('\u2014', '--')

with open('/home/z/my-project/src/ui/Theme.cpp', 'w', encoding='utf-8') as f:
    f.write(content)

# Verify
matches = re.findall(r'R"\((.*?)\)"', content, re.DOTALL)
print(f'After splitting: {len(matches)} raw string literals')
for i, m in enumerate(matches):
    print(f'  Raw string {i}: {len(m)} chars')
