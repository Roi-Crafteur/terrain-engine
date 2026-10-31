"""The planet file of a version, beside the files of the user and never over them.

The planet files of the machine (machine.json "planets") and those built before in
<workRoot>/planets/ are checked with terrain_probe --planet-check of the version, which only reads
them: the first one it accepts is copied into the project of the version
(Saved/TerrainEngine/planet_<seed>.bin), the path the driver gives to PlanetFile. Without one,
terrain_make_planet of the version builds it in <workRoot>/planets/, named by its checksum. A
version without --planet-check (before terrain_probe) builds its own, named by its commit.
"""

import json
import re
import shutil
from dataclasses import dataclass
from pathlib import Path

_CHECKSUM = re.compile(r"checksum (?:0x)?([0-9a-f]{16})")


class PlanetError(RuntimeError):
    """A planet file that would be written over a file of the user, or that cannot be made."""


@dataclass
class PlanetChoice:
    path: Path           # the file read (a planet of the machine, or one of <workRoot>/planets)
    checksum: str
    origin: str          # "machine", "built before" or "built now"


def _same(a, b):
    return Path(a).resolve() == Path(b).resolve()


def guard(path, machine):
    """Refuses to write path when it is a planet file of the user."""
    for planet in machine.planets:
        if _same(path, planet):
            raise PlanetError(f"refusing to write {path}: a planet file of the user (machine "
                              "planets), only ever read")


def check(runner, version, path, seed):
    """terrain_probe --planet-check of the version on path: (status, checksum)."""
    code, out = runner.run([version.probe, "--planet-check", path, "--seed", seed, "--json"],
                           check=False)
    if runner.dry:
        return "ok", "0x0000000000000000"
    try:
        result = json.loads(out)["planetCheck"]
    except (ValueError, KeyError):
        return "unreadable", None
    return result["status"], result.get("checksum")


def candidates(machine):
    built = sorted((machine.work_root / "planets").glob("*.bin"))
    return [Path(p) for p in machine.planets] + built


def choose(machine, version, runner, log=None):
    """The planet file this version reads (checked, or built now)."""
    planets = machine.work_root / "planets"
    if version.probe_kind == "probe":
        for path in candidates(machine):
            if not runner.dry and not path.is_file():
                continue
            status, checksum = check(runner, version, path, machine.seed)
            if status == "ok":
                origin = "machine" if path in machine.planets else "built before"
                return PlanetChoice(path, checksum, origin)
    target = planets / (f"building_{version.short}.bin" if version.probe_kind == "probe"
                        else f"commit_{version.sha}.bin")
    if version.probe_kind != "probe" and target.exists():
        return PlanetChoice(target, _checksum_of_log(log), "built before")
    guard(target, machine)
    planets.mkdir(parents=True, exist_ok=True)
    _, out = runner.run([version.make_planet, target, "--seed", machine.seed], log)
    found = _CHECKSUM.search(out)
    checksum = "0x" + found.group(1) if found else None
    if version.probe_kind == "probe" and checksum and not runner.dry:
        named = planets / f"{checksum[2:]}.bin"
        target.replace(named)
        target = named
    return PlanetChoice(target, checksum, "built now")


def _checksum_of_log(log):
    if log is None or not Path(log).exists():
        return None
    found = _CHECKSUM.findall(Path(log).read_text(encoding="utf-8", errors="replace"))
    return "0x" + found[-1] if found else None


def install(choice, version, machine, dry=False):
    """The copy of the chosen planet in the project of the version, the path PlanetFile gets."""
    target = version.project_dir / "Saved" / "TerrainEngine" / f"planet_{machine.seed}.bin"
    guard(target, machine)
    if not dry:
        target.parent.mkdir(parents=True, exist_ok=True)
        if not (target.exists() and target.stat().st_size == choice.path.stat().st_size and
                _same_bytes_head(target, choice.path)):
            shutil.copyfile(choice.path, target)
    return target.resolve() if not dry else target


def _same_bytes_head(a, b, size=1 << 16):
    with open(a, "rb") as fa, open(b, "rb") as fb:
        return fa.read(size) == fb.read(size)
