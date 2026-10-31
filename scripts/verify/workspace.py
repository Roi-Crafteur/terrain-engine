"""The versions of a verification, each built from scratch once and kept by commit.

<workRoot>/mirror.git              a mirror of the repository (fetched at each run)
<workRoot>/cache/<sha12>/project/  a fresh copy of the host project; Plugins/<plugin> is a clone
                                   of the commit, Binaries and Intermediate built there by UBT
<workRoot>/cache/<sha12>/tools/    the tools of the core (scripts/verify/tools/CMakeLists.txt)
<workRoot>/cache/<sha12>/build_ok.json   written last: the cache of this commit is complete
(sha12: the first 12 digits of the commit, so that the paths of UBT stay short under Windows)

A commit whose cache is complete is not built again (--fresh: removed and built again). The host
project is copied without Binaries, Intermediate, Saved and the DDC; the folders of Content
listed in linkContent are linked (mklink /J) to the real project of the user, except those a
script or the driver writes to (the PACKAGE_PATH of the material scripts, the folder of the map),
which are always copied.
"""

import json
import os
import platform
import re
import shutil
import subprocess
from dataclasses import asdict, dataclass, field
from pathlib import Path

import ue_log

HERE = Path(__file__).resolve().parent
TOOLS_PROJECT = HERE / "tools"
SKIPPED = {"Binaries", "Intermediate", "Saved", "DerivedDataCache", ".vs", ".git", ".idea"}
MARKER = "build_ok.json"
_PACKAGE_PATH = re.compile(r'PACKAGE_PATH\s*=\s*"/Game/([^/"]+)')


class WorkspaceError(RuntimeError):
    """A step of the preparation of a version that failed, with what to read."""


class Runner:
    """Runs the commands of the preparation, each into its log; dry: only records them."""

    def __init__(self, dry=False):
        self.dry = dry
        self.commands = []

    def run(self, command, log=None, cwd=None, check=True):
        command = [str(c) for c in command]
        self.commands.append(command)
        if self.dry:
            return 0, ""
        if log is not None:
            Path(log).parent.mkdir(parents=True, exist_ok=True)
        done = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, errors="replace")
        if log is not None:
            with open(log, "a", encoding="utf-8") as out:
                out.write("> " + " ".join(command) + "\n" + done.stdout + "\n")
        if check and done.returncode != 0:
            tail = "\n".join(done.stdout.splitlines()[-20:])
            raise WorkspaceError(f"{' '.join(command)} failed ({done.returncode}), see "
                                 f"{log}:\n{tail}")
        return done.returncode, done.stdout


@dataclass
class Version:
    label: str
    ref: str
    sha: str
    cache: Path
    project_dir: Path
    uproject: Path
    plugin_dir: Path
    tools_dir: Path
    probe: Path = None
    probe_kind: str = ""          # "probe" (terrain_probe) or "probe_water" (before it)
    make_planet: Path = None
    features: dict = field(default_factory=dict)
    build: dict = field(default_factory=dict)
    built_now: bool = False

    @property
    def short(self):
        return self.sha[:7]

    @property
    def scripts_dir(self):
        return self.plugin_dir / "terrain_unreal" / "Scripts"


# ---- what is copied and what may be linked ----------------------------------------------------

def written_content_folders(scripts_dir):
    """The top folders of Content the material scripts of a version write to (PACKAGE_PATH)."""
    folders = set()
    for script in sorted(Path(scripts_dir).glob("*.py")):
        folders.update(_PACKAGE_PATH.findall(script.read_text(encoding="utf-8", errors="replace")))
    return folders


def never_linked(machine, scripts_dir):
    return {"TerrainEngine", machine.map_folder()} | written_content_folders(scripts_dir)


def check_links(link_content, never):
    refused = sorted(set(link_content) & set(never))
    if refused:
        raise WorkspaceError(
            f"linkContent names {', '.join(refused)}: the material scripts or the driver write "
            "there, so it must be copied, never linked to the project of the user")


def make_link(source, target):
    """A link of a folder: a junction (mklink /J) under Windows, a symbolic link elsewhere."""
    if platform.system() == "Windows":
        subprocess.run(["cmd", "/c", "mklink", "/J", str(target), str(source)], check=True,
                       stdout=subprocess.DEVNULL)
    else:
        os.symlink(source, target, target_is_directory=True)


def _ignore(directory, names):
    return [n for n in names if n in SKIPPED or n.endswith(".sln")]


def copy_host_project(machine, target, never, linker=make_link):
    """A fresh copy of the host project in target (whose Plugins/<plugin> may already hold the
    clone): what is copied, what is linked, as a list of lines."""
    check_links(machine.link_content, never)
    source = machine.project.parent
    target = Path(target)
    target.mkdir(parents=True, exist_ok=True)
    done = []
    for entry in sorted(source.iterdir()):
        name = entry.name
        if name in SKIPPED or name.endswith(".sln"):
            continue
        if entry.is_file():
            shutil.copy2(entry, target / name)
            done.append(f"copied {name}")
        elif name == "Plugins":
            done += _copy_plugins(machine, entry, target / name)
        elif name == "Content":
            done += _copy_content(machine, entry, target / name, linker)
        else:
            shutil.copytree(entry, target / name, ignore=_ignore, dirs_exist_ok=True)
            done.append(f"copied {name}/")
    return done


def _copy_plugins(machine, source, target):
    done = []
    target.mkdir(parents=True, exist_ok=True)
    for plugin in sorted(source.iterdir()):
        if plugin.name == machine.plugin_folder or not plugin.is_dir():
            continue
        shutil.copytree(plugin, target / plugin.name, ignore=_ignore, dirs_exist_ok=True)
        done.append(f"copied Plugins/{plugin.name}/")
    return done


def _copy_content(machine, source, target, linker):
    done = []
    target.mkdir(parents=True, exist_ok=True)
    for entry in sorted(source.iterdir()):
        if entry.is_dir() and entry.name in machine.link_content:
            linker(entry.resolve(), target / entry.name)
            done.append(f"linked Content/{entry.name}/ -> {entry.resolve()}")
        elif entry.is_dir():
            shutil.copytree(entry, target / entry.name, dirs_exist_ok=True)
            done.append(f"copied Content/{entry.name}/")
        else:
            shutil.copy2(entry, target / entry.name)
    return done


def _is_link(path):
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or (is_junction is not None and is_junction())


def remove_tree(path, work_root):
    """Removes a folder of the work, its links first (never what they point to)."""
    path = Path(path).resolve()
    root = Path(work_root).resolve()
    if root not in path.parents:
        raise WorkspaceError(f"refusing to remove {path}: outside the work folder {root}")
    if not path.exists():
        return
    for content in path.glob("**/Content"):
        for entry in content.iterdir():
            if _is_link(entry):
                os.unlink(entry) if entry.is_symlink() else os.rmdir(entry)
    shutil.rmtree(path)


# ---- git ---------------------------------------------------------------------------------------

def mirror(machine, runner, log):
    path = machine.work_root / "mirror.git"
    if (path / "HEAD").exists() or runner.dry:
        runner.run(["git", "-C", path, "remote", "update", "--prune"], log)
    else:
        runner.run(["git", "clone", "--mirror", machine.repository, path], log)
    return path


def resolve(runner, mirror_path, ref, log):
    """The commit of a branch, a tag, a commit or a pull request ("pull/57" or "#57")."""
    found = re.fullmatch(r"(?:#|pull/)(\d+)", ref)
    candidates = [f"refs/pull/{found.group(1)}/head"] if found else [ref, f"refs/heads/{ref}"]
    for candidate in candidates:
        code, out = runner.run(["git", "-C", mirror_path, "rev-parse", "--verify", "--quiet",
                                f"{candidate}^{{commit}}"], log, check=False)
        if runner.dry:
            return ref
        if code == 0 and out.strip():
            return out.strip().splitlines()[-1]
    raise WorkspaceError(f"no commit for '{ref}' in {mirror_path}")


# ---- builds ------------------------------------------------------------------------------------

def probe_target(plugin_dir):
    """terrain_probe, terrain_probe_water before it, or None."""
    path = Path(plugin_dir) / "terrain_core" / "CMakeLists.txt"
    text = path.read_text(encoding="utf-8") if path.exists() else ""
    if "add_executable(terrain_probe " in text:
        return "terrain_probe"
    return "terrain_probe_water" if "add_executable(terrain_probe_water " in text else None


def build_commands(machine, uproject, plugin_dir, tools_dir, probe, system=None):
    configure = ["cmake", "-S", TOOLS_PROJECT, "-B", tools_dir, f"-DTERRAIN_REPO={plugin_dir}",
                 "-DCMAKE_BUILD_TYPE=Release"]
    if machine.cmake_generator:
        configure += ["-G", machine.cmake_generator]
    targets = [t for t in (probe, "terrain_make_planet") if t]
    return {
        "plugin": [machine.build_script(system), machine.editor_target, machine.platform,
                   machine.configuration, f"-Project={uproject}", "-WaitMutex",
                   "-NoHotReloadFromIDE"],
        "configure": configure,
        "tools": ["cmake", "--build", tools_dir, "--config", "Release", "--parallel",
                  "--target", *targets],
    }


def find_tool(tools_dir, name):
    for folder in ("terrain_core/Release", "terrain_core"):
        for suffix in (".exe", ""):
            path = Path(tools_dir) / folder / (name + suffix)
            if path.is_file():
                return path
    return None


def plugin_features(plugin_dir):
    sources = Path(plugin_dir) / "terrain_unreal" / "Source"
    texts = [p.read_text(encoding="utf-8", errors="replace")
             for p in sorted(sources.glob("**/*")) if p.suffix in (".cpp", ".h", ".hpp")]
    return ue_log.plugin_features(texts)


def _finish(version):
    probe = probe_target(version.plugin_dir)
    version.probe_kind = {"terrain_probe": "probe", "terrain_probe_water": "probe_water"}.get(
        probe, "")
    version.probe = find_tool(version.tools_dir, probe) if probe else None
    version.make_planet = find_tool(version.tools_dir, "terrain_make_planet")
    version.features = plugin_features(version.plugin_dir)
    return version


def _build(machine, version, runner, logs):
    commands = build_commands(machine, version.uproject, version.plugin_dir, version.tools_dir,
                              probe_target(version.plugin_dir))
    code, out = runner.run(commands["plugin"], logs / "ubt.log", check=False)
    version.build = ue_log.scan_build(out) if not runner.dry else {"result": "dry"}
    if code != 0 and not runner.dry:
        version.build["result"] = "Failed"
        raise WorkspaceError(f"the plugin of {version.label} ({version.short}) did not build: "
                             f"{logs / 'ubt.log'}\n" + "\n".join(version.build["errors"][:10]))
    runner.run(commands["configure"], logs / "tools.log")
    runner.run(commands["tools"], logs / "tools.log")


def prepare(machine, ref, label, runner, fresh=False):
    """The version of ref, from its cache or built now."""
    logs = machine.work_root / "logs"
    mirror_path = mirror(machine, runner, logs / "git.log")
    sha = resolve(runner, mirror_path, ref, logs / "git.log")
    cache = machine.work_root / "cache" / sha[:12]  # short: the paths of UBT stay under 260
    project = cache / "project"
    version = Version(label, ref, sha, cache, project, project / machine.project.name,
                      project / "Plugins" / machine.plugin_folder, cache / "tools")
    marker = cache / MARKER
    if fresh and cache.exists() and not runner.dry:
        remove_tree(cache, machine.work_root)
    if marker.exists() and not runner.dry:
        version.build = json.loads(marker.read_text(encoding="utf-8")).get("build", {})
        return _finish(version)
    if cache.exists() and not runner.dry:
        remove_tree(cache, machine.work_root)  # an incomplete build: never reused
    runner.run(["git", "clone", "--no-hardlinks", "--no-checkout", mirror_path,
                version.plugin_dir], logs / "git.log")
    runner.run(["git", "-C", version.plugin_dir, "checkout", "--detach", sha], logs / "git.log")
    if not runner.dry:
        never = never_linked(machine, version.scripts_dir)
        copied = copy_host_project(machine, project, never)
        (cache / "copied.txt").write_text("\n".join(copied) + "\n", encoding="utf-8")
    _build(machine, version, runner, cache / "logs")
    version.built_now = True
    if runner.dry:  # what the tools of a version with terrain_probe would be
        suffix = ".exe" if platform.system() == "Windows" else ""
        folder = version.tools_dir / "terrain_core"
        version.probe_kind = "probe"
        version.probe = folder / f"terrain_probe{suffix}"
        version.make_planet = folder / f"terrain_make_planet{suffix}"
    else:
        _finish(version)
        marker.write_text(json.dumps({"sha": sha, "ref": ref, "build": version.build},
                                     indent=2), encoding="utf-8")
    return version


def describe(version):
    out = asdict(version)
    return {k: str(v) if isinstance(v, Path) else v for k, v in out.items()}
