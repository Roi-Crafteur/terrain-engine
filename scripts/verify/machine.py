"""The machine the verification runs on (machine.json, beside this file by default; see
machine.example.json): where Unreal and the host project are, the test map, the planet files that
may be reused (read and copied, never written), the folder of the work.

The work never goes under the temporary folder of the system: incremental MSBuild builds there
have mixed the binaries of two versions (MSB8029).
"""

import json
import os
import platform
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_FILE = HERE / "machine.json"
SUN_PRESETS = {"falls": {"elevation": 50.0, "yaw": 116.0}}


class MachineError(ValueError):
    """A machine file that cannot be used."""


@dataclass
class Machine:
    engine: Path
    project: Path               # the .uproject of the host project, copied for each version
    map: str                    # /Game/... of the test map
    work_root: Path
    target: str = ""
    platform: str = "Win64"
    configuration: str = "Development"
    plugin_folder: str = "terrain-engine"
    repository: str = "https://github.com/Roi-Crafteur/terrain-engine.git"
    planets: list = field(default_factory=list)       # reused when valid, never written
    link_content: list = field(default_factory=list)  # folders of Content linked, not copied
    editor_args: list = field(default_factory=list)
    cmake_generator: str = ""
    seed: int = 20261019
    sun: dict = field(default_factory=lambda: dict(SUN_PRESETS))

    @property
    def project_name(self):
        return self.project.stem

    @property
    def editor_target(self):
        return self.target or f"{self.project_name}Editor"

    def editor_exe(self, system=None):
        system = system or platform.system()
        binaries = self.engine / "Engine" / "Binaries"
        if system == "Windows":
            return binaries / "Win64" / "UnrealEditor.exe"
        if system == "Darwin":
            return binaries / "Mac" / "UnrealEditor.app" / "Contents" / "MacOS" / "UnrealEditor"
        return binaries / "Linux" / "UnrealEditor"

    def build_script(self, system=None):
        system = system or platform.system()
        batch = self.engine / "Engine" / "Build" / "BatchFiles"
        if system == "Windows":
            return batch / "Build.bat"
        return batch / ("Mac" if system == "Darwin" else "Linux") / "Build.sh"

    def map_folder(self):
        """The top folder of Content that holds the test map (/Game/TerrainTest/... ->
        TerrainTest)."""
        parts = [p for p in self.map.split("/") if p]
        if len(parts) < 3 or parts[0] != "Game":
            raise MachineError(f"the map must be /Game/<folder>/<name>, not '{self.map}'")
        return parts[1]


def _temporary_roots():
    roots = {Path(tempfile.gettempdir()).resolve()}
    for name in ("TEMP", "TMP", "TMPDIR"):
        if os.environ.get(name):
            roots.add(Path(os.environ[name]).resolve())
    return roots


def check_work_root(path):
    path = Path(path)
    if not path.is_absolute():
        raise MachineError(f"workRoot must be an absolute folder, not '{path}'")
    resolved = path.resolve()
    for root in _temporary_roots():
        if resolved == root or root in resolved.parents:
            raise MachineError(f"workRoot {path} lies in the temporary folder {root}: "
                               "incremental builds there have mixed versions (MSB8029)")
    if " " in str(path):
        raise MachineError(f"workRoot {path} holds a space: Unreal takes the path of the "
                           "driver on its command line")


def parse_machine(data, check_root=True):
    """The machine of the decoded JSON; check_root False only in the tests (their folders lie in
    the temporary folder)."""
    try:
        machine = Machine(
            engine=Path(data["engine"]), project=Path(data["project"]), map=data["map"],
            work_root=Path(data["workRoot"]), target=data.get("target", ""),
            platform=data.get("platform", "Win64"),
            configuration=data.get("configuration", "Development"),
            plugin_folder=data.get("pluginFolder", "terrain-engine"),
            repository=data.get("repository", Machine.repository),
            planets=[Path(p) for p in data.get("planets", [])],
            link_content=list(data.get("linkContent", [])),
            editor_args=list(data.get("editorArgs", [])),
            cmake_generator=data.get("cmakeGenerator", ""),
            seed=int(data.get("seed", 20261019)))
    except KeyError as missing:
        raise MachineError(f"machine file without {missing}") from None
    machine.sun.update(data.get("sun", {}))
    if machine.project.suffix != ".uproject":
        raise MachineError(f"project must be a .uproject, not '{machine.project}'")
    machine.map_folder()
    if check_root:
        check_work_root(machine.work_root)
    return machine


def load_machine(path=DEFAULT_FILE):
    path = Path(path)
    if not path.exists():
        raise MachineError(f"no machine file {path}: copy machine.example.json to it and "
                           "set its paths")
    with open(path, encoding="utf-8") as file:
        try:
            return parse_machine(json.load(file))
        except json.JSONDecodeError as error:
            raise MachineError(f"{path}: not JSON ({error})") from error
