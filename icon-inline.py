#!/usr/bin/env python3

import os
import re
import urllib.parse

HTML_FILE = "src/webui/index.html"
ICON_DIR = "src/webui/icons"

BUILD_DIR = "src/generated"
OUT_FILE = os.path.join(BUILD_DIR, "index.html.icon")

pattern = re.compile(r'(["\'])/icons/([^"\']+\.svg)\1')


def inline_icon(match):
    quote = match.group(1)
    filename = match.group(2)

    path = os.path.join(ICON_DIR, filename)

    with open(path, "r", encoding="utf-8") as f:
        svg = f.read()

    # Collapse whitespace for smaller output.
    svg = " ".join(svg.split())

    data = urllib.parse.quote(svg, safe="")

    print(f"Inlining {filename}")

    return f'{quote}data:image/svg+xml,{data}{quote}'


os.makedirs(BUILD_DIR, exist_ok=True)

with open(HTML_FILE, "r", encoding="utf-8") as f:
    html = f.read()

html = pattern.sub(inline_icon, html)

with open(OUT_FILE, "w", encoding="utf-8", newline="\n") as f:
    f.write(html)

print(f"Generated {OUT_FILE}")
