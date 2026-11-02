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
        self.notes = []  # what a dry run has to say (a mirror out of date)

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

    def query(self, command, cwd=None):
        """A command that only reads (git rev-parse, git ls-remote): run even when dry."""
        command = [str(c) for c in command]
        self.commands.append(command)
        try:
            done = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, errors="replace")
        except OSError:
            return 1, ""
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
    """The folders of Content always copied: those the material scripts write to, the folder of
    the map, and Python (the launcher writes init_unreal.py there)."""
    return ({"TerrainEngine", "Python", machine.map_folder()} |
            written_content_folders(scripts_dir))


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


def _candidates(ref):
    found = re.fullmatch(r"(?:#|pull/)(\d+)", ref)
    return [f"refs/pull/{found.group(1)}/head"] if found else [ref, f"refs/heads/{ref}",
                                                               f"refs/tags/{ref}"]


def _in_mirror(runner, mirror_path, ref):
    for candidate in _candidates(ref):
        code, out = runner.query(["git", "-C", mirror_path, "rev-parse", "--verify", "--quiet",
                                  f"{candidate}^{{commit}}"])
        if code == 0 and out.strip():
            return out.strip().splitlines()[-1]
    return None


def _in_repository(runner, repository, ref):
    names = _candidates(ref) if ref.startswith(("#", "pull/")) else _candidates(ref)[1:]
    code, out = runner.query(["git", "ls-remote", repository, *names])
    lines = [line.split() for line in out.splitlines() if line.strip()]
    return lines[0][0] if code == 0 and lines else None


def resolve(runner, mirror_path, ref, log, repository=None):
    """The commit of a branch, a tag, a commit or a pull request ("pull/57" or "#57"), read in
    the mirror (updated just before by a real run). A dry run only reads: a branch, a tag or a
    pull request by git ls-remote of the repository (the mirror may be out of date: said in
    runner.notes), a commit in the mirror; None when it cannot tell."""
    has_mirror = (Path(mirror_path) / "HEAD").exists()
    if not runner.dry:
        found = _in_mirror(runner, mirror_path, ref)
        if found:
            return found
        raise WorkspaceError(f"no commit for '{ref}' in {mirror_path}")
    mirrored = _in_mirror(runner, mirror_path, ref) if has_mirror else None
    if re.fullmatch(r"[0-9a-fA-F]{7,40}", ref) or not repository:
        return mirrored
    remote = _in_repository(runner, repository, ref)
    if remote and mirrored and remote != mirrored:
        runner.notes.append(f"the mirror is out of date for {ref}: {mirrored[:12]} there, "
                            f"{remote[:12]} in the repository (a real run fetches it first)")
    if remote is None and mirrored:
        runner.notes.append(f"{ref} not found by git ls-remote: {mirrored[:12]} of the mirror, "
                            "which may be out of date")
    return remote or mirrored


def cache_name(sha, ref):
    """The folder of the cache of a commit: its first 12 digits (the paths of UBT stay short
    under Windows), or the ref without its separators when a dry run could not resolve it."""
    return sha[:12] if sha else re.sub(r"[^A-Za-z0-9._-]+", "-", ref).strip("-") or "ref"


# ---- builds ------------------------------------------------------------------------------------

def probe_target(plugin_dir):
    """terrain_probe, terrain_probe_water before it, or None."""
    path = Path(plugin_dir) / "terrain_core" / "CMakeLists.txt"
    text = path.read_text(encoding="utf-8") if path.exists() else ""
    if "add_executable(terrain_probe " in text:
        return "terrain_probe"
    return "terrain_probe_water" if "add_executable(terrain_probe_water " in text else None


def find_cmake(machine, which=shutil.which):
    """cmake: the one of machine.json first, then the one of the PATH, then the one pip
    installed (the module cmake). None without one."""
    if machine.cmake:
        return Path(machine.cmake) if Path(machine.cmake).is_file() else None
    found = which("cmake")
    if found:
        return Path(found)
    try:
        import cmake as pip_cmake  # noqa: PLC0415 - optional
    except ImportError:
        return None
    name = "cmake.exe" if platform.system() == "Windows" else "cmake"
    path = Path(getattr(pip_cmake, "CMAKE_BIN_DIR", "")) / name
    return path if path.is_file() else None


def build_commands(machine, uproject, plugin_dir, tools_dir, probe, system=None, cmake="cmake"):
    configure = [cmake, "-S", TOOLS_PROJECT, "-B", tools_dir, f"-DTERRAIN_REPO={plugin_dir}",
                 "-DCMAKE_BUILD_TYPE=Release"]
    if machine.cmake_generator:
        configure += ["-G", machine.cmake_generator]
    targets = [t for t in (probe, "terrain_make_planet") if t]
    return {
        "plugin": [machine.build_script(system), machine.editor_target, machine.platform,
                   machine.configuration, f"-Project={uproject}", "-WaitMutex",
                   "-NoHotReloadFromIDE"],
        "configure": configure,
        "tools": [cmake, "--build", tools_dir, "--config", "Release", "--parallel",
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
    cmake = find_cmake(machine)
    if cmake is None and not runner.dry:
        raise WorkspaceError("no cmake: set \"cmake\" in machine.json (the path of cmake.exe), "
                             "or put cmake in the PATH")
    commands = build_commands(machine, version.uproject, version.plugin_dir, version.tools_dir,
                              probe_target(version.plugin_dir), cmake=cmake or "cmake")
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
    sha = resolve(runner, mirror_path, ref, logs / "git.log", machine.repository)
    cache = machine.work_root / "cache" / cache_name(sha, ref)
    project = cache / "project"
    version = Version(label, ref, sha or ref, cache, project, project / machine.project.name,
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
    runner.run(["git", "-C", version.plugin_dir, "checkout", "--detach", version.sha],
               logs / "git.log")
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


def install_start(machine, version, start_file):
    """init_unreal.py in Content/Python of the copy of the project of a version: the startup
    script that starts the driver (start_driver.py); never in the project of the user. An
    init_unreal.py the project had is kept as init_unreal_project.py, which ours runs first."""
    folder = version.project_dir / "Content" / "Python"
    user = machine.project.parent.resolve()
    target = (folder / "init_unreal.py").resolve()
    if user == target or user in target.parents:
        raise WorkspaceError(f"refusing to write {target}: in the project of the user")
    folder.mkdir(parents=True, exist_ok=True)
    text = Path(start_file).read_text(encoding="utf-8")
    current = folder / "init_unreal.py"
    if current.is_file():
        existing = current.read_text(encoding="utf-8", errors="replace")
        marker = text.splitlines()[0]
        if marker not in existing:
            current.replace(folder / "init_unreal_project.py")
    current.write_text(text, encoding="utf-8")
    return current


def describe(version):
    out = asdict(version)
    return {k: str(v) if isinstance(v, Path) else v for k, v in out.items()}
