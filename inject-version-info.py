#!/usr/bin/env python3

import os
import re
import subprocess

HTML_FILE = "src/generated/index.html.icon"
BUILD_INFO_FILE = "src/generated/build_info.hpp"

BUILD_DIR = "src/generated"
OUT_FILE = os.path.join(BUILD_DIR, "index.html")


def get_git_version():
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        return "unknown"


def get_build_date():
    pattern = re.compile(
        r'#define\s+BUILD_LAST_MODIFIED\s+"([^"]+)"'
    )

    with open(BUILD_INFO_FILE, "r", encoding="utf-8") as f:
        text = f.read()

    match = pattern.search(text)

    if match:
        return match.group(1)

    return "unknown"


git_version = get_git_version()
build_date = get_build_date()

with open(HTML_FILE, "r", encoding="utf-8") as f:
    html = f.read()

print(f"Injecting GIT_VERSION = {git_version}")
html = html.replace("{{GIT_VERSION}}", git_version)

print(f"Injecting BUILD_LAST_MODIFIED = {build_date}")
html = html.replace("{{BUILD_LAST_MODIFIED}}", build_date)

os.makedirs(BUILD_DIR, exist_ok=True)

with open(OUT_FILE, "w", encoding="utf-8", newline="\n") as f:
    f.write(html)

print(f"Generated {OUT_FILE}")
