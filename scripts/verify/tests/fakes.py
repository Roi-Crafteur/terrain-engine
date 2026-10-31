"""Fakes shared by the tests of the launcher: a host project on disk, and a runner that answers the
commands of a preparation (git, UBT, cmake, the probes) without running them."""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import machine as machine_module  # noqa: E402
import workspace  # noqa: E402

SHA = "0123456789abcdef0123456789abcdef01234567"


def host_project(root):
    """A host project like TerrainSandbox: what is copied, linked and skipped."""
    project = Path(root) / "TerrainSandbox"
    for folder in ("Config", "Source/TerrainSandbox", "Content/TerrainTest",
                   "Content/TerrainEngine", "Content/BigPack", "Plugins/terrain-engine/x",
                   "Plugins/Other/Binaries", "Binaries", "Intermediate", "Saved/TerrainEngine",
                   "DerivedDataCache"):
        (project / folder).mkdir(parents=True, exist_ok=True)
    (project / "TerrainSandbox.uproject").write_text("{}", encoding="utf-8")
    (project / "TerrainSandbox.sln").write_text("", encoding="utf-8")
    (project / "Config" / "DefaultEngine.ini").write_text("[x]", encoding="utf-8")
    (project / "Content" / "TerrainTest" / "TerrainGasp.umap").write_bytes(b"map")
    (project / "Content" / "TerrainEngine" / "M_TerrainWater.uasset").write_bytes(b"mat")
    (project / "Content" / "BigPack" / "T_Rock.uasset").write_bytes(b"tex")
    (project / "Plugins" / "Other" / "Other.uplugin").write_text("{}", encoding="utf-8")
    (project / "Saved" / "TerrainEngine" / "planet_20261019.bin").write_bytes(b"user planet")
    return project


def a_machine(root, **changes):
    root = Path(root)
    project = host_project(root)
    data = {"engine": str(root / "UE_5.8"), "project": str(project / "TerrainSandbox.uproject"),
            "map": "/Game/TerrainTest/TerrainGasp", "workRoot": str(root / "work"),
            "planets": [str(project / "Saved" / "TerrainEngine" / "planet_20261019.bin")]}
    data.update(changes)
    return machine_module.parse_machine(data, check_root=False)


class FakeRunner(workspace.Runner):
    """Answers the commands as the tools would, and makes the files they would make."""

    def __init__(self, probe_kind="probe", planet_status=None, build_ok=True):
        super().__init__(dry=False)
        self.probe_kind = probe_kind
        self.planet_status = planet_status or {}
        self.build_ok = build_ok

    def run(self, command, log=None, cwd=None, check=True):
        command = [str(c) for c in command]
        self.commands.append(command)
        text = " ".join(command)
        if "rev-parse" in command:
            return 0, SHA + "\n"
        if command[:2] == ["git", "clone"] and "--mirror" not in command:
            self._clone(Path(command[-1]))
            return 0, ""
        if command[0].endswith(("Build.bat", "Build.sh")):
            return (0, "Result: Succeeded\n") if self.build_ok else (6, "x.cpp(1): error C1\n")
        if command[:2] == ["cmake", "--build"]:
            self._tools(Path(command[2]))
            return 0, ""
        if "--planet-check" in command:
            path = command[command.index("--planet-check") + 1]
            status = self.planet_status.get(Path(path).name, "stale")
            check = {"status": status, "checksum": "0x26626564570b0d9e" if status == "ok" else None}
            return (0 if status == "ok" else 2), json.dumps({"planetCheck": check})
        if "terrain_make_planet" in text:
            Path(command[1]).write_bytes(b"built planet")
            return 0, f"{command[1]}: built, checksum 1111222233334444\n"
        return 0, ""

    def _clone(self, target):
        core = target / "terrain_core"
        core.mkdir(parents=True, exist_ok=True)
        tool = "terrain_probe" if self.probe_kind == "probe" else "terrain_probe_water"
        (core / "CMakeLists.txt").write_text(f"add_executable({tool} tools/x.cpp)\n",
                                             encoding="utf-8")
        scripts = target / "terrain_unreal" / "Scripts"
        scripts.mkdir(parents=True, exist_ok=True)
        (scripts / "create_water_materials.py").write_text('PACKAGE_PATH = "/Game/TerrainEngine"\n',
                                                           encoding="utf-8")
        source = target / "terrain_unreal" / "Source"
        source.mkdir(parents=True, exist_ok=True)
        (source / "Loading.cpp").write_text('TEXT("Terrain World: load %d report")',
                                            encoding="utf-8")

    def _tools(self, tools):
        folder = tools / "terrain_core"
        folder.mkdir(parents=True, exist_ok=True)
        for name in ("terrain_probe", "terrain_probe_water", "terrain_make_planet"):
            (folder / name).write_text("", encoding="utf-8")
