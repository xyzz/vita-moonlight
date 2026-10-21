#!/usr/bin/env python3
"""Smoke-test a Moonlight Vita VPK without needing a connected console.

Checks the packaged launcher and both selectable frontend executables.
Run after a real VitaSDK build; this does not exercise runtime streaming.
"""
import argparse
import hashlib
import sys
import zipfile
from pathlib import Path


REQUIRED_FILES = (
    "eboot.bin",
    "legacy.self",
    "borealis.self",
    "sce_sys/param.sfo",
    "sce_sys/icon0.png",
    "sce_sys/livearea/contents/template.xml",
    "assets/nerdfont.ttf",
)
EXECUTABLES = ("eboot.bin", "legacy.self", "borealis.self")


def validate_vpk(path):
    """Return a list of descriptive failures (empty means packaging passes)."""
    path = Path(path)
    if not path.is_file():
        return [f"VPK does not exist: {path}"]

    errors = []
    try:
        with zipfile.ZipFile(path, "r") as archive:
            entries = archive.infolist()
            names = [entry.filename for entry in entries if not entry.is_dir()]
            duplicates = sorted({name for name in names if names.count(name) > 1})
            if duplicates:
                errors.append("Duplicate VPK entries: " + ", ".join(duplicates))

            for name in REQUIRED_FILES:
                if name not in names:
                    errors.append(f"Missing packaged file: {name}")
                elif archive.getinfo(name).file_size == 0:
                    errors.append(f"Empty packaged file: {name}")

            bad = archive.testzip()
            if bad is not None:
                errors.append(f"Corrupt VPK entry (CRC mismatch): {bad}")

            if all(name in names and archive.getinfo(name).file_size > 0 for name in EXECUTABLES):
                digests = {}
                for name in EXECUTABLES:
                    digest = hashlib.sha256()
                    with archive.open(name) as entry:
                        for chunk in iter(lambda: entry.read(1024 * 1024), b""):
                            digest.update(chunk)
                    digests[name] = digest.hexdigest()
                if len(set(digests.values())) != len(EXECUTABLES):
                    errors.append("Launcher, Legacy and Borealis must be distinct executables")
    except (OSError, zipfile.BadZipFile, zipfile.LargeZipFile, EOFError) as error:
        errors.append(f"Unreadable VPK: {error}")
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description="Verify the combined Moonlight Vita VPK")
    parser.add_argument("vpk", type=Path, help="path to the compiled moonlight.vpk")
    args = parser.parse_args(argv)

    problems = validate_vpk(args.vpk)
    if problems:
        for problem in problems:
            print(f"[VPK] ERROR: {problem}", file=sys.stderr)
        return 1
    print(f"[VPK] PASS: launcher, Legacy, Borealis and Vita metadata in {args.vpk}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
