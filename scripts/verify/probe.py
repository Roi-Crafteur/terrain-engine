"""What the core of a version says of the places of a verification: the ground of the origin of
the Unreal frame and the water there (for the eye), and the ground along a walk. With
terrain_probe (its JSON); before it, from the text of terrain_probe_water (the ground and the
water only, no line).

The planet file given is always the copy in the project of the version: terrain_probe_water and
terrain_probe --planet build and write the file when it is stale.
"""

import json
import re
from dataclasses import dataclass

import places as places_module
from planet import guard

_POINT = re.compile(r"point \(([-\d.]+), ([-\d.]+)\):\n  relief ([-\d.]+) m, ground ([-\d.]+) m; "
                    r"water: (dry|sea|river|lake|salt lake)(?: at ([-\d.]+) m)?")


class ProbeError(RuntimeError):
    """A probe that did not answer."""


@dataclass(frozen=True)
class Column:
    ground: float           # the ground of the world (the relief with its water): the frame
    relief: float           # the relief alone
    water: str              # "dry", "sea", "river", "lake", "salt lake"
    level: float = None     # the water level, None on dry land


def _planet_args(planet_file, machine):
    if planet_file is None:
        return []
    guard(planet_file, machine)
    return ["--planet", planet_file]


def parse_probe_water(text):
    """The columns of the text of terrain_probe_water, in the order of its points."""
    out = []
    for m in _POINT.finditer(text):
        level = float(m.group(6)) if m.group(6) else None
        out.append(Column(float(m.group(4)), float(m.group(3)), m.group(5), level))
    return out


def parse_probe_json(text):
    data = json.loads(text)
    out = []
    for p in data["points"]:
        w = p["water"]
        out.append(Column(p["relief"]["ground"], p["relief"]["relief"], w["kind"], w["level"]))
    return out


def columns(runner, version, machine, points, planet_file=None, log=None):
    """The columns of the core at the points (x, z), with the planet file or without it."""
    if not points:
        return []
    coords = [str(c) for xz in points for c in xz]
    planet = _planet_args(planet_file, machine)
    if version.probe_kind == "probe":
        command = [version.probe, *planet, "--seed", machine.seed, "--json", "--sections",
                   "relief,water", *coords]
        parse = parse_probe_json
    elif version.probe_kind == "probe_water":
        command = [version.probe, *planet, "--seed", machine.seed, "--falls", "0", *coords]
        parse = parse_probe_water
    else:
        raise ProbeError(f"{version.label} has no probe of the core")
    _, out = runner.run(command, log)
    if runner.dry:
        return [Column(0.0, 0.0, "dry") for _ in points]
    found = parse(out)
    if len(found) != len(points):
        raise ProbeError(f"the probe of {version.label} gave {len(found)} points of "
                         f"{len(points)}")
    return found


def frame_of(column, place):
    """The origin of the frame of a place (CoreOriginX/Z on it) and the eye: the altitude of
    the ground there (Unreal Z = 0) and the altitude of the eye."""
    eye = places_module.eye_altitude(place, column.ground, column.level)
    return {"ground": column.ground, "waterLevel": column.level, "eye": eye,
            "eyeZ": 100.0 * (eye - column.ground)}


def walk_profile(runner, version, machine, start, heading, distance, planet_file=None, log=None,
                 step=2.0):
    """The ground of the core along a walk, [(distance, ground)], or None before terrain_probe."""
    if version.probe_kind != "probe":
        return None
    dx, dz = places_module.forward(heading)
    x2, z2 = start[0] + distance * dx, start[1] + distance * dz
    command = [version.probe, *_planet_args(planet_file, machine), "--seed", machine.seed,
               "--json", "--sections", "relief", "--line", start[0], start[1], x2, z2, step]
    _, out = runner.run(command, log)
    if runner.dry:
        return []
    line = json.loads(out)["line"]
    return [(p["distance"], p["relief"]["ground"]) for p in line["points"]]
