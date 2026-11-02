"""The check that a verification left the project of the user as it was, without git (the host
project is no repository): before and after a run, the size and the time of the last write of
every file of the project (its Content with the folders the copies link to, Config, Source, its
plugins but ours), and the SHA256 of the planet files of the machine. Binaries, Intermediate,
Saved and the DDC of the project are left out: the editor of the user may write there."""

import hashlib
import os
from pathlib import Path

LEFT_OUT = {"Binaries", "Intermediate", "Saved", "DerivedDataCache", ".vs", ".git", ".idea"}


def _files(root, plugin_folder):
    out = {}
    for folder, dirs, names in os.walk(root, followlinks=False):
        here = Path(folder)
        relative = here.relative_to(root)
        dirs[:] = sorted(d for d in dirs if d not in LEFT_OUT and not (
            relative == Path("Plugins") and d == plugin_folder))
        for name in names:
            path = here / name
            try:
                stat = path.stat()
            except OSError:
                continue
            out[(relative / name).as_posix()] = [stat.st_size, stat.st_mtime_ns]
    return out


def sha256(path):
    digest = hashlib.sha256()
    try:
        with open(path, "rb") as file:
            for block in iter(lambda: file.read(1 << 20), b""):
                digest.update(block)
    except OSError:
        return None
    return digest.hexdigest()


def snapshot(machine):
    root = machine.project.parent
    return {"root": str(root), "files": _files(root, machine.plugin_folder),
            "planets": {str(p): sha256(p) for p in machine.planets}}


def compare(before, after):
    """What changed between two snapshots: files changed, added and removed, planets changed."""
    a, b = before["files"], after["files"]
    return {
        "files": len(b),
        "changed": sorted(k for k in a.keys() & b.keys() if a[k] != b[k]),
        "added": sorted(b.keys() - a.keys()),
        "removed": sorted(a.keys() - b.keys()),
        "planets": {p: before["planets"].get(p) == after["planets"].get(p)
                    for p in before["planets"]},
    }


def untouched(diff):
    return (not diff["changed"] and not diff["added"] and not diff["removed"] and
            all(diff["planets"].values()))
