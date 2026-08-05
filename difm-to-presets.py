#!/usr/bin/env python3

#
# Converts a DI.FM .pls playlist into:
#   src/generated/private_presets.inc
#
# and automatically injects:
#   -D USE_PRIVATE_PRESETS
#
# into the PlatformIO build.
#
# Expected usage:
#   - place a .pls file in project root
#   - add this script as a pre: extra_script
#
# Generated file is gitignored.
#

Import("env")

import os
import glob
import configparser

OUTPUT_FILE = "src/generated/private_presets.inc"


def parse_pls(path):
    config = configparser.ConfigParser(interpolation=None)
    config.read(path, encoding="utf-8")

    if "playlist" not in config:
        raise ValueError(f"{path}: missing [playlist] section")

    playlist = config["playlist"]

    entries = []

    i = 1
    while True:
        file_key = f"File{i}"
        title_key = f"Title{i}"

        if file_key not in playlist:
            break

        url = playlist[file_key].strip()
        title = playlist.get(title_key, f"Stream {i}").strip()

        # escape quotes
        title = title.replace('"', '\\"')
        url = url.replace('"', '\\"')

        entries.append((title, url))

        i += 1

    return entries


def find_pls_files():
    return glob.glob("*.pls")


def generate_include(entries):
    lines = []

    lines.append("// auto-generated from .pls")
    lines.append("// do not edit manually")
    lines.append("")

    for title, url in entries:
        lines.append(f'{{"{title}", "{url}"}},')

    lines.append("")

    return "\n".join(lines)


def main():
    pls_files = find_pls_files()

    if not pls_files:
        print("[di.fm] no .pls file found")
        return

    all_entries = []

    for pls in pls_files:
        print(f"[di.fm] parsing {pls}")

        try:
            entries = parse_pls(pls)
            all_entries.extend(entries)

        except Exception as e:
            print(f"[di.fm] error: {e}")

    if not all_entries:
        print("[di.fm] no valid entries found")
        return

    content = generate_include(all_entries)

    os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"[di.fm] generated {OUTPUT_FILE}")
    print(f"[di.fm] added {len(all_entries)} private presets")

    #
    # inject compiler define
    #
    env.Append(
        CPPDEFINES=[
            "USE_PRIVATE_PRESETS"
        ]
    )

    print("[di.fm] injected USE_PRIVATE_PRESETS")


main()