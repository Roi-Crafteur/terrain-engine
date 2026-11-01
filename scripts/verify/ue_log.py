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


def load_ended(text, starts_logged=True):
    """The load that ended in this part of the log: the last one started in it ("Terrain World:
    load N (reason) started at (x, z) m"), once its report follows; {"n", "reason", "x", "z"},
    or None while it runs. The report of a load started before does not count: a teleport first
    reports the previous load as it stands, then starts its own. Only for a plugin that logs no
    start (starts_logged False) does any report count."""
    starts = list(_LOAD_START.finditer(text))
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
    start and the report of each load (agent 38) and the console commands of the dev tools (agent 36), each
    found by the text of its registration (TEXT("Terrain.Goto")...)."""
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
        "loadTest": command("Terrain.LoadTest"),
    }
