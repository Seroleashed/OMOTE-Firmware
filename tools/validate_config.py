#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["jsonschema>=4.21"]
# ///
"""Validate OMOTE configuration files against the JSON schemas in schema/.

    uv run tools/validate_config.py devices_library/*.json
    uv run tools/validate_config.py --all

The schema is picked by the file's own "type" field, so the same command works
for a device pack, a system file, scenes and the keypad matrix. That is also the
first thing checked: a file without a recognised type is reported as such rather
than validated against something it never claimed to be.

This is the second opinion on the firmware's own parser. The firmware decides
what it accepts; this script decides what we are willing to publish. When the
two disagree, one of them has a bug - which is the point of having both.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator

REPO_ROOT = Path(__file__).resolve().parent.parent
SCHEMA_DIR = REPO_ROOT / "schema"

SCHEMA_BY_TYPE = {
    "omote.devicePack": "devicePack.schema.json",
    "omote.system": "system.schema.json",
    "omote.scenes": "scenes.schema.json",
    "omote.keys": "keys.schema.json",
    "omote.ui": "ui.schema.json",
}

# where configuration files live when --all is used
SEARCH_PATHS = ["devices_library", "cfg"]


def load_schema(name: str) -> dict:
    with (SCHEMA_DIR / name).open(encoding="utf-8") as handle:
        return json.load(handle)


def validate_file(path: Path) -> list[str]:
    """Returns a list of problems; empty means the file is fine."""
    try:
        with path.open(encoding="utf-8") as handle:
            document = json.load(handle)
    except json.JSONDecodeError as error:
        return [f"not valid JSON: {error}"]

    if not isinstance(document, dict):
        return ["expected a JSON object at the top level"]

    file_type = document.get("type")
    if file_type is None:
        return ["no 'type' field, cannot tell what kind of file this is"]
    if file_type not in SCHEMA_BY_TYPE:
        known = ", ".join(sorted(SCHEMA_BY_TYPE))
        return [f"unknown type '{file_type}', expected one of: {known}"]

    validator = Draft202012Validator(load_schema(SCHEMA_BY_TYPE[file_type]))
    problems = []
    for error in sorted(validator.iter_errors(document), key=lambda e: list(e.path)):
        location = "/".join(str(part) for part in error.path) or "(root)"
        problems.append(f"{location}: {error.message}")
    return problems


def collect_files(arguments: list[str]) -> list[Path]:
    if arguments == ["--all"]:
        files: list[Path] = []
        for folder in SEARCH_PATHS:
            files.extend(sorted((REPO_ROOT / folder).glob("**/*.json")))
        return files
    return [Path(argument) for argument in arguments]


def main() -> int:
    arguments = sys.argv[1:]
    if not arguments:
        print(__doc__)
        return 2

    files = collect_files(arguments)
    if not files:
        print("no files to validate")
        return 1

    failed = 0
    for path in files:
        problems = validate_file(path)
        if problems:
            failed += 1
            print(f"FAIL  {path}")
            for problem in problems:
                print(f"        {problem}")
        else:
            print(f"ok    {path}")

    print()
    if failed:
        print(f"{failed} of {len(files)} file(s) invalid")
        return 1
    print(f"all {len(files)} file(s) valid")
    return 0


if __name__ == "__main__":
    sys.exit(main())
