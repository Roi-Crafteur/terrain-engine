#!/usr/bin/env python3
"""Checks the project rules that need no build. Usage: scripts/check_project_rules.py

Fails (exit code 1) when a .cpp or .hpp file of terrain_core, terrain_viewer or tests is longer
than 500 lines, or when a source file of terrain_core includes a graphics or Unreal header
(OpenGL, glad, GLFW, Vulkan, Direct3D, CoreMinimal.h). The limit of 80 lines per function is not
checked. Each violation is printed as path:line and as a GitHub annotation.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAX_LINES = 500
SIZE_CHECKED_DIRS = ("terrain_core", "terrain_viewer", "tests")
SIZE_CHECKED_SUFFIXES = {".cpp", ".hpp"}
CORE_SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".c", ".inl"}

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
GL_DIRS = re.compile(r"gl|gles\d*|opengl")
GL_HEADERS = {"gl.h", "glu.h", "glext.h", "glcorearb.h"}


def forbidden_api(header):
    """Returns the graphics or Unreal API of an included path, or None. Compares the components
    of the path one by one, so that names such as global.hpp are not mistaken for OpenGL."""
    parts = header.lower().replace("\\", "/").split("/")
    if parts[0].startswith("glad"):
        return "glad"
    if parts[0].startswith("glfw"):
        return "GLFW"
    if parts[0].startswith("vulkan"):
        return "Vulkan"
    if any(GL_DIRS.fullmatch(d) for d in parts[:-1]) or parts[-1] in GL_HEADERS:
        return "OpenGL"
    if parts[-1].startswith("d3d"):
        return "Direct3D"
    if parts[-1] == "coreminimal.h":
        return "Unreal"
    return None


def source_files(directory, suffixes):
    return sorted(p for p in (ROOT / directory).rglob("*") if p.is_file() and p.suffix in suffixes)


def check_sizes():
    violations = []
    checked = 0
    for directory in SIZE_CHECKED_DIRS:
        for path in source_files(directory, SIZE_CHECKED_SUFFIXES):
            checked += 1
            count = len(path.read_text(encoding="utf-8").splitlines())
            if count > MAX_LINES:
                violations.append((path, 1, f"{count} lines, more than {MAX_LINES}"))
    return checked, violations


def check_core_includes():
    violations = []
    checked = 0
    for path in source_files("terrain_core", CORE_SOURCE_SUFFIXES):
        checked += 1
        lines = path.read_text(encoding="utf-8").splitlines()
        for number, line in enumerate(lines, start=1):
            match = INCLUDE.match(line)
            api = forbidden_api(match.group(1)) if match else None
            if api:
                message = f"terrain_core must not include {match.group(1)} ({api})"
                violations.append((path, number, message))
    return checked, violations


def main():
    size_count, size_violations = check_sizes()
    core_count, include_violations = check_core_includes()
    violations = size_violations + include_violations
    for path, line, message in violations:
        relative = path.relative_to(ROOT).as_posix()
        print(f"{relative}:{line}: {message}")
        print(f"::error file={relative},line={line}::{message}")
    print(f"{size_count} files checked for size, {core_count} terrain_core files checked for "
          f"includes, {len(violations)} violation(s)")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
