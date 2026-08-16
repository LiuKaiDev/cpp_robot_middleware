#!/usr/bin/env python3
"""Validate repository-local links in README and docs Markdown files."""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from urllib.parse import unquote


REPO_ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
SKIP_SCHEMES = ("http://", "https://", "mailto:", "data:")


def markdown_files(paths: list[Path]) -> list[Path]:
    files: set[Path] = set()
    for path in paths:
        resolved = path if path.is_absolute() else REPO_ROOT / path
        if resolved.is_dir():
            files.update(resolved.rglob("*.md"))
        elif resolved.suffix == ".md":
            files.add(resolved)
    return sorted(files)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="*", type=Path, default=[Path("README.md"), Path("docs")])
    args = parser.parse_args()
    failures: list[str] = []
    files = markdown_files(args.paths)
    for source in files:
        text = source.read_text(encoding="utf-8")
        for match in LINK.finditer(text):
            target = match.group(1).strip().split(maxsplit=1)[0].strip("<>")
            if not target or target.startswith("#") or target.startswith(SKIP_SCHEMES):
                continue
            path_text = unquote(target.split("#", 1)[0])
            target_path = Path(path_text)
            resolved = target_path if target_path.is_absolute() else source.parent / target_path
            if not resolved.exists():
                line = text.count("\n", 0, match.start()) + 1
                failures.append(f"{source.relative_to(REPO_ROOT)}:{line}: {target}")
    if failures:
        print("broken local Markdown links:")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print(f"validated {len(files)} Markdown files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
