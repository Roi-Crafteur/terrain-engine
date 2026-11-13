"""What the logs of Unreal say: the build of UnrealBuildTool (its result and its errors), and the
log of the editor (the Python errors of the material scripts, the errors of the shaders, the
self-check of the determinism of the plugin, the planet file read or built, the loads of the
world reported by the plugin, the errors of the terrain)."""

import re

_UBT_RESULT = re.compile(r"Result:\s*(Succeeded|Failed|Cancelled)", re.IGNORECASE)
_UBT_ERROR = re.compile(r"(: (fatal )?error [A-Z]*\d*:|\berror (C|LNK|MSB)\d{4}\b|"
                        r"^ERROR:|: error:)", re.IGNORECASE)
_UBT_WARNING = re.compile(r"(: warning [A-Z]*\d*:|\bwarning C\d{4}\b|: warning:)",
                          re.IGNORECASE)

_PYTHON_ERROR = re.compile(r"LogPython: Error|^Traceback \(most recent call last\)|"
                           r"LogPython:.*Error:")
_SHADER_ERROR = re.compile(r"LogShaderCompilers: (Error|Warning: .*error)|"
                           r"Shader compile error|Failed to compile Material|"
                           r"\[SM\d\].*\berror\b|LogMaterial: Error", re.IGNORECASE)
_DETERMINISM_OK = "match terrain_core bit for bit"
_DETERMINISM_BAD = "does not generate the same world"
_PLANET = re.compile(r"planet level (read from|BUILT)[^\n]*")
_LOAD_REPORT = re.compile(r"Terrain World: load (\d+) report")
_LOAD_START = re.compile(r"Terrain World: load (\d+) \(([^)]*)\) started at "
                         r"\((-?\d+(?:\.\d+)?), (-?\d+(?:\.\d+)?)\) m")
_PAWN_STANDS = re.compile(r"Terrain dev: the pawn of the game stands, spot \d+ of \d+")
_PAWN_SLID = re.compile(r"Terrain dev: Terrain\.Pawn: the pawn of the game slid from the [^\n]*")
_PAWN_REFUSED = re.compile(r"Terrain dev: (Terrain\.Pawn: (over water|no walkable ground|none of "
                           r"the|the view is still loading)[^\n]*)")
_PAWN_ALREADY = "Terrain dev: the player already has the pawn of the game"
# Terrain.Map.Go (agent 35, TerrainDevMapLanding.cpp): the landing decided, the line of the check
# a second after the pawn of the game is given back, the ends without it.
_NUMBER = r"(-?\d+(?:\.\d+)?)"
_CHECK = re.compile(r"Terrain Dev Map: (\d+) s after the click: (the pawn of the game|the view of "
                    r"the tools), ([^,]+), " + _NUMBER + r" m/s, feet at " + _NUMBER +
                    r" m \(([^)]*)\), " + _NUMBER + r" m from the click: (OK|NOT walking[^\n]*)")
_DOWN = re.compile(r": (shallow water: )?the pawn of the game goes down near (?:the dry ground at )?"
                   r"x " + _NUMBER + r", z " + _NUMBER)
_WATER = re.compile(r": over water \(no dry ground within " + _NUMBER + r" m\)[^\n]*")
_FALLBACK = re.compile(r"Terrain Dev Map: (no spot near the dry ground holds the pawn: back[^\n]*)")
_REFUSED = re.compile(r"(?:Terrain Dev Map: )?((?:The teleport to |No landing for |The view did not "
                      r"load in time|The view of the tools stays|Terrain\.Map\.Go: )[^\n]*)")
# Sent before the world of the play was ready: no landing at all, a problem of the launcher.
_NOT_READY = re.compile(r"(Terrain\.Map\.Go: no world playing[^\n]*)")
_PLANET_WARNING = re.compile(r"Terrain Dev Map: (\S+) (was measured with(?:out)? the planet "
                             r"file[^\n]*)")
_TERRAIN_PROBLEM = re.compile(r"LogTerrainEngine: (Error|Warning): (.*)")
_CRASH = re.compile(r"Fatal error|Unhandled Exception|=== Critical error: ===")


def scan_build(text):
    """The build of UnrealBuildTool: {"result", "errors" (lines), "warnings" (count)}."""
    result = None
    errors = []
    warnings = 0
    for line in text.splitlines():
        found = _UBT_RESULT.search(line)
        if found:
            result = found.group(1).capitalize()
        if _UBT_ERROR.search(line):
            errors.append(line.strip())
        elif _UBT_WARNING.search(line):
            warnings += 1
    if result is None and errors:
        result = "Failed"
    return {"result": result, "errors": errors, "warnings": warnings}


def load_reports(text):
    """The numbers of the loads the plugin reported ("Terrain World: load N report"), in order."""
    return [int(m.group(1)) for m in _LOAD_REPORT.finditer(text)]


def load_ended(text, starts_logged=True, reasons=None):
    """The load that ended in this part of the log: the last one started in it ("Terrain World:
    load N (reason) started at (x, z) m"), once its report follows; {"n", "reason", "x", "z"},
    or None while it runs. The report of a load started before does not count: a teleport first
    reports the previous load as it stands, then starts its own. With reasons, only the loads of
    these reasons count (after Terrain.Goto: "teleport", never the spawn load of the play). Only
    for a plugin that logs no start (starts_logged False) does any report count."""
    starts = [m for m in _LOAD_START.finditer(text) if reasons is None or m.group(2) in reasons]
    reports = list(_LOAD_REPORT.finditer(text))
    if not starts:
        if starts_logged or not reports:
            return None
        return {"n": int(reports[-1].group(1)), "reason": None, "x": None, "z": None}
    last = starts[-1]
    n = int(last.group(1))
    if not any(int(r.group(1)) == n and r.start() > last.start() for r in reports):
        return None
    return {"n": n, "reason": last.group(2), "x": float(last.group(3)),
            "z": float(last.group(4))}


def _landing_check(text):
    found = _CHECK.search(text)
    if not found:
        return None
    return {"seconds": float(found.group(1)), "who": found.group(2), "mode": found.group(3),
            "speed": float(found.group(4)), "feet": float(found.group(5)),
            "water": found.group(6), "away": float(found.group(7)),
            "ok": found.group(8) == "OK", "line": found.group(0)[len("Terrain Dev Map: "):]}


def landing_end(text, name):
    """What Terrain.Map.Go <name> wrote in this part of the log (read from just before the
    command): {"decision" (land, shallow, water or None), "x", "z" (where the view goes),
    "radius" (the dry ground looked for, m), "check" (the line a second after the pawn is
    given back), "end" (check, water, fallback or refused), "message", "planetWarning"}; None
    while the landing runs. Over water there is no line of the check: the decision ends it."""
    out = {"decision": None, "x": None, "z": None, "radius": None, "check": None, "end": None,
           "message": None, "planetWarning": None}
    for warning in _PLANET_WARNING.finditer(text):
        if warning.group(1) == name:
            out["planetWarning"] = warning.group(2).strip()
    prefix = re.compile(r"Terrain Dev Map: " + re.escape(name) + r"(: [^\n]*)")
    for said in prefix.finditer(text):
        down, water = _DOWN.match(said.group(1)), _WATER.match(said.group(1))
        if down:
            out.update(decision="shallow" if down.group(1) else "land", x=float(down.group(2)),
                       z=float(down.group(3)))
        elif water:
            out.update(decision="water", radius=float(water.group(1)))
        out["message"] = said.group(1)[2:].strip()
    out["check"] = _landing_check(text)
    fallback, refused = _FALLBACK.search(text), _REFUSED.search(text)
    early = _NOT_READY.search(text)
    if out["check"]:
        out["end"] = "check"
    elif early:
        out.update(end="notReady", message=early.group(1).strip())
    elif fallback:
        out.update(end="fallback", message=fallback.group(1).strip())
        water = _WATER.search(": " + fallback.group(1).split("back ", 1)[-1])
        out["radius"] = float(water.group(1)) if water else out["radius"]
    elif refused:
        out.update(end="refused", message=refused.group(1).strip())
    elif out["decision"] == "water":
        out["end"] = "water"
    return out if out["end"] else None


def pawn_landing(text):
    """What Terrain.Pawn (agent 36) did, from this part of the log: {"state": "stands" (put down
    and standing), "slid" (it slid from every spot tried and stays where it is), "refused" (over
    water, no walkable ground, the view still loading: the flying pawn of the tools stays) or
    "already" (the player had the pawn of the game), "message"}, or None while it lands."""
    refused = _PAWN_REFUSED.search(text)
    if refused:
        return {"state": "refused", "message": refused.group(1).strip()}
    slid = _PAWN_SLID.search(text)
    if slid:
        return {"state": "slid", "message": slid.group(0).strip()}
    stands = _PAWN_STANDS.search(text)
    if stands:
        return {"state": "stands", "message": stands.group(0).strip()}
    if _PAWN_ALREADY in text:
        return {"state": "already", "message": _PAWN_ALREADY}
    return None


def scan_editor(text):
    """The log of a session of the editor."""
    lines = text.splitlines()
    determinism = None
    if _DETERMINISM_BAD in text:
        determinism = False
    elif _DETERMINISM_OK in text:
        determinism = True
    problems = [m.group(1) + ": " + m.group(2).strip() for m in _TERRAIN_PROBLEM.finditer(text)]
    return {
        "pythonErrors": [l.strip() for l in lines if _PYTHON_ERROR.search(l)],
        "shaderErrors": [l.strip() for l in lines if _SHADER_ERROR.search(l)],
        "determinism": determinism,
        "planet": [m.group(0).strip() for m in _PLANET.finditer(text)],
        "planetBuilt": any("BUILT" in m.group(0) for m in _PLANET.finditer(text)),
        "loadReports": load_reports(text),
        "terrainProblems": problems,
        "crash": [l.strip() for l in lines if _CRASH.search(l)],
    }


def read_text(path):
    """A log as text, whatever its encoding (the logs of Unreal are UTF-8, those of the build of
    Windows may hold other bytes)."""
    with open(path, "rb") as file:
        data = file.read()
    for encoding in ("utf-8", "utf-16"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue
    return data.decode("latin-1")


def plugin_features(source_texts):
    """What the plugin of a version offers to the driver, from the texts of its sources: the
    start and the report of each load (agent 38) and the console commands of the dev tools (agent 36,
    agent 41), each found by the text of its registration (TEXT("Terrain.Goto")...)."""
    joined = "\n".join(source_texts)

    def command(name):
        return f'TEXT("{name}")' in joined

    return {
        "loadReport": "load %d report" in joined,
        "loadStart": "started at (%.1f, %.1f) m" in joined,
        "goto": command("Terrain.Goto"),
        "look": command("Terrain.Look"),
        "viewMode": command("Terrain.View"),
        "pawn": command("Terrain.Pawn"),
        "pawnLanding": "the pawn of the game stands, spot" in joined,
        "mapGo": command("Terrain.Map.Go"),
        "mapStarted": "Terrain Dev Map: started (" in joined,
        "determinism": _DETERMINISM_OK in joined,
        "loadTest": command("Terrain.LoadTest"),
        # The edits of the scripts (agent 41, TerrainDevEdits.cpp): a click by its code, and the
        # saved edits read by the next play.
        "editAim": command("Terrain.EditAim"),
        "editRead": command("Terrain.EditRead"),
    }
