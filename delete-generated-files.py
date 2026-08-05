#!/usr/bin/env python3

from pathlib import Path

BUILD_DIR = Path("src/generated")

BUILD_DIR.mkdir(exist_ok=True)

for path in BUILD_DIR.iterdir():
    if path.is_file():
        print(f"Removing {path}")
        path.unlink()

print(f"Cleaned {BUILD_DIR}")
