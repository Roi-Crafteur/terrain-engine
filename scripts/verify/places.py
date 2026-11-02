"""The places shared by the dev tools: data/dev/places.json (format version 1).

One file for the agents 35 (dev map, teleport), 36 (in-game debug tools) and 37 (this launcher):

    { "version": 1,
      "places": [
        { "name": "chute388_bassin", "x": -6185104, "z": 155924,
          "heading": 227, "pitch": 30, "eye": 182.03, "eyeMode": "altitude",
          "planet": true, "note": "..." } ] }

- x, z: coordinates of the engine (terrain_core), meters;
- heading: degrees clockwise from the north (-z of the core, -Y of Unreal); Unreal yaw is
  heading - 90;
- pitch: degrees, positive up (the tools of agent 20 give a tangent: tilt_to_pitch);
- eye: meters; eyeMode "altitude" (absolute altitude of the core) or "ground" (above the ground,
  or above the water where there is water);
- planet: the view needs the planet file (rivers, lakes and falls).

The Unreal frame (terrain_unreal/Source/TerrainEngine/Private/Bridge/unreal_frame.hpp): with
CoreOriginX/Z on (ox, oz), Unreal (0, 0, 0) is the ground of the core there (oy); a point of the
core (x, y, z) is X = 100 (x - ox), Y = 100 (z - oz), Z = 100 (y - oy) centimeters.
"""

import json
import math
from dataclasses import dataclass
from pathlib import Path

FORMAT_VERSION = 1
EYE_MODES = ("altitude", "ground")
REPOSITORY = Path(__file__).resolve().parents[2]
DEFAULT_FILE = REPOSITORY / "data" / "dev" / "places.json"


class PlacesError(ValueError):
    """A places file that does not hold version 1 of the format, or an unknown place."""


@dataclass(frozen=True)
class Place:
    name: str
    x: float
    z: float
    heading: float
    pitch: float
    eye: float
    eye_mode: str
    planet: bool
    note: str = ""


def _number(entry, key, where):
    value = entry.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise PlacesError(f"{where}: '{key}' must be a finite number, not {value!r}")
    return float(value)


def _place(entry, index):
    where = f"place #{index + 1}"
    if not isinstance(entry, dict):
        raise PlacesError(f"{where}: an object expected")
    name = entry.get("name")
    if not isinstance(name, str) or not name or any(c.isspace() for c in name):
        raise PlacesError(f"{where}: 'name' must be a word, not {name!r}")
    where = f"place '{name}'"
    mode = entry.get("eyeMode")
    if mode not in EYE_MODES:
        raise PlacesError(f"{where}: 'eyeMode' must be one of {EYE_MODES}, not {mode!r}")
    planet = entry.get("planet")
    if not isinstance(planet, bool):
        raise PlacesError(f"{where}: 'planet' must be true or false")
    pitch = _number(entry, "pitch", where)
    if not -90.0 <= pitch <= 90.0:
        raise PlacesError(f"{where}: 'pitch' must lie in [-90, 90] degrees, not {pitch}")
    note = entry.get("note", "")
    if not isinstance(note, str):
        raise PlacesError(f"{where}: 'note' must be a text")
    return Place(name, _number(entry, "x", where), _number(entry, "z", where),
                 _number(entry, "heading", where), pitch, _number(entry, "eye", where), mode,
                 planet, note)


def parse_places(data):
    """The places of the decoded JSON of a places file, checked."""
    if not isinstance(data, dict) or data.get("version") != FORMAT_VERSION:
        raise PlacesError(f"a places file of version {FORMAT_VERSION} expected")
    entries = data.get("places")
    if not isinstance(entries, list):
        raise PlacesError("'places' must be a list")
    places = [_place(entry, i) for i, entry in enumerate(entries)]
    seen = set()
    for place in places:
        if place.name in seen:
            raise PlacesError(f"place '{place.name}' given twice")
        seen.add(place.name)
    return places


def load_places(path=DEFAULT_FILE):
    with open(path, encoding="utf-8") as file:
        try:
            data = json.load(file)
        except json.JSONDecodeError as error:
            raise PlacesError(f"{path}: not JSON ({error})") from error
    return parse_places(data)


def select(places, names):
    """The places named, in the order given ("all" or an empty list: every place)."""
    if not names or names == "all" or names == ["all"]:
        return list(places)
    if isinstance(names, str):
        names = [n for n in names.split(",") if n]
    by_name = {p.name: p for p in places}
    unknown = [n for n in names if n not in by_name]
    if unknown:
        raise PlacesError(f"unknown place(s) {', '.join(unknown)}; known: "
                          f"{', '.join(sorted(by_name))}")
    return [by_name[n] for n in names]


def unreal_yaw(heading):
    """The yaw of Unreal for a heading of the core, degrees in (-180, 180]."""
    yaw = math.fmod(heading - 90.0, 360.0)
    if yaw <= -180.0:
        yaw += 360.0
    elif yaw > 180.0:
        yaw -= 360.0
    return yaw


def forward(heading):
    """The unit direction (dx, dz) of the core along a heading."""
    turn = math.radians(heading)
    return math.sin(turn), -math.cos(turn)


def tilt_to_pitch(tilt):
    """The pitch in degrees of a tilt given as a tangent (terrain_export_rock --at, --steps)."""
    return math.degrees(math.atan(tilt))


def eye_altitude(place, ground, water_level=None):
    """The altitude of the eye of a place in the core, from the ground (and the water) there."""
    if place.eye_mode == "altitude":
        return place.eye
    top = ground if water_level is None else max(ground, water_level)
    return top + place.eye


def core_to_unreal(x, y, z, origin_x, origin_y, origin_z):
    """Centimeters of Unreal of the point (x, y, z) of the core, the frame on (origin_x, origin_z)
    whose ground is origin_y."""
    return 100.0 * (x - origin_x), 100.0 * (z - origin_z), 100.0 * (y - origin_y)


def unreal_to_core(ux, uy, uz, origin_x, origin_y, origin_z):
    """The point (x, y, z) of the core of Unreal centimeters, the inverse of core_to_unreal."""
    return origin_x + ux / 100.0, origin_y + uz / 100.0, origin_z + uy / 100.0
