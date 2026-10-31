"""The verification of a version, or of one against another, in Unreal: one command
(scripts/verify/README.md).

  python scripts/verify/verify.py run --machine scripts/verify/machine.json \\
      --before main --after <branch> --places chute388_bassin,fleuve_plaine --sun falls \\
      --walk chute388_bassin:227:60
  python scripts/verify/verify.py probe --probe <terrain_probe> [--planet <file>] <place>...

run: each version from the cache of its commit or built now (a fresh clone, a fresh copy of the
host project, UBT, the tools of the core), its planet file checked and copied beside, the places
probed with its core (the eye of Unreal), short sessions of the editor under the watchdog, then
<workRoot>/runs/<date>_<before>_vs_<after>/: resume.txt, planche.png, planche.html, the views,
the walks, the logs.
"""

import argparse
import datetime
import json
import re
import shutil
import sys
from dataclasses import asdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import machine as machine_module  # noqa: E402
import places as places_module  # noqa: E402
import planet as planet_module  # noqa: E402
import probe as probe_module  # noqa: E402
import report  # noqa: E402
import session  # noqa: E402
import ue_log  # noqa: E402
import walks as walks_module  # noqa: E402
import workspace  # noqa: E402

DRIVER_FILES = ["ue_driver.py", "ue_api.py", "ue_log.py", "walks.py"]
# The order of terrain_unreal/WATER_LEVEL.md: the horizon material again after the water.
MATERIAL_SCRIPTS = ["create_temporary_terrain_material.py", "create_horizon_gpu_material.py",
                    "create_water_materials.py", "create_horizon_gpu_material.py"]


def parser():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run", help="views, walks and their summary in Unreal")
    run.add_argument("--machine", default=str(machine_module.DEFAULT_FILE))
    run.add_argument("--before", required=True, help="branch, tag, commit or #<pull request>")
    run.add_argument("--after", help="the version compared to --before (none: one version)")
    run.add_argument("--places", default="all", help="names of data/dev/places.json, a,b,c")
    run.add_argument("--places-file", default=str(places_module.DEFAULT_FILE))
    run.add_argument("--planet", choices=("auto", "yes", "no"), default="auto",
                     help="auto: the planet file for the places that need it")
    run.add_argument("--sun", default="none", help="none, a preset (falls) or elevation,yaw")
    run.add_argument("--walk", action="append", default=[],
                     help="place[:heading[:meters]], the first fall stops it")
    run.add_argument("--view-mode", default="normal", help="Terrain.View, when the version has it")
    run.add_argument("--resolution", default="1600x900")
    run.add_argument("--settle", type=float, default=20.0, help="seconds before a screenshot")
    run.add_argument("--first-settle", type=float, default=45.0,
                     help="seconds before the first one without a load report")
    run.add_argument("--places-per-session", type=int, default=6)
    run.add_argument("--console", action="append", default=[], help="a command before each view")
    run.add_argument("--session-seconds", type=float, default=3600.0)
    run.add_argument("--hang-seconds", type=float, default=300.0)
    for flag, what in (("--no-materials", "do not run the material scripts"),
                       ("--no-freeze", "do not pause the game before the screenshots"),
                       ("--no-repeat", "no second screenshot of each view"),
                       ("--no-pitch-check", "no check view of the pitch"),
                       ("--no-dev-commands", "never Terrain.Goto / Terrain.Look"),
                       ("--offscreen", "-RenderOffScreen (never tried)"),
                       ("--fresh", "build the versions again, their cache removed"),
                       ("--dry-run", "print the commands, run nothing")):
        run.add_argument(flag, action="store_true", help=what)
    prb = sub.add_parser("probe", help="terrain_probe on named places")
    prb.add_argument("--probe", default=None, help="terrain_probe (default: build/terrain_core)")
    prb.add_argument("--planet", default=None, help="a planet file (read, built when stale)")
    prb.add_argument("--places-file", default=str(places_module.DEFAULT_FILE))
    prb.add_argument("--json", action="store_true")
    prb.add_argument("names", nargs="+")
    return p


def parse_sun(text, machine):
    if text in ("", "none"):
        return None
    if text in machine.sun:
        return dict(machine.sun[text])
    try:
        elevation, yaw = (float(v) for v in text.split(","))
    except ValueError:
        raise SystemExit(f"--sun: none, {', '.join(machine.sun)} or elevation,yaw, not '{text}'")
    return {"elevation": elevation, "yaw": yaw}


def use_planet(place, mode):
    return {"yes": True, "no": False}.get(mode, place.planet)


def slug(text):
    return re.sub(r"[^A-Za-z0-9._-]+", "-", text).strip("-")[:40] or "x"


def place_jobs(runner, v, m, chosen, mode, planet_file, log):
    """The places as the driver gets them, from the probes of the core of the version."""
    jobs, frames = [], {}
    for planet_wanted in (True, False):
        group = [p for p in chosen if use_planet(p, mode) == planet_wanted]
        columns = probe_module.columns(runner, v, m, [(p.x, p.z) for p in group],
                                       planet_file if planet_wanted else None, log)
        for p, column in zip(group, columns):
            frame = probe_module.frame_of(column, p)
            frames[p.name] = frame
            jobs.append({"name": p.name, "x": p.x, "z": p.z, "heading": p.heading,
                         "pitch": p.pitch, "yaw": places_module.unreal_yaw(p.heading),
                         "eye": p.eye, "eyeMode": p.eye_mode, "usePlanet": planet_wanted,
                         "ground": frame["ground"], "waterLevel": frame["waterLevel"],
                         "eyeAltitude": frame["eye"]})
    order = [p.name for p in chosen]
    return sorted(jobs, key=lambda j: order.index(j["name"])), frames


def walk_jobs(runner, v, m, asked, by_name, mode, planet_file, log):
    jobs, profiles = [], {}
    for w in asked:
        p = by_name[w["place"]]
        heading = p.heading if w["heading"] is None else w["heading"]
        profiles[p.name] = probe_module.walk_profile(
            runner, v, m, (p.x, p.z), heading, w["distance"],
            planet_file if use_planet(p, mode) else None, log)
        jobs.append({"place": p.name, "heading": heading, "distance": w["distance"],
                     "yaw": places_module.unreal_yaw(heading)})
    return jobs, profiles


def base_job(args, v, m, folder, planet_file, sun):
    width, height = (int(n) for n in args.resolution.lower().split("x"))
    return {"label": v.label, "sha": v.sha, "out": str(folder), "scripts": str(v.scripts_dir),
            "materials": not args.no_materials, "materialScripts": MATERIAL_SCRIPTS,
            "editorLog": str(v.project_dir / "Saved" / "Logs" / f"{m.project_name}.log"),
            "features": v.features, "planetFile": str(planet_file or ""),
            "resolution": [width, height], "settle": args.settle,
            "firstSettle": args.first_settle, "freeze": not args.no_freeze,
            "repeatShot": not args.no_repeat, "pitchCheck": not args.no_pitch_check,
            "devCommands": not args.no_dev_commands, "viewMode": args.view_mode,
            "console": args.console, "sun": sun, "quit": True}


def run_sessions(args, v, m, job, chosen_jobs, walk_list, folder, driver_dir):
    """The sessions of a version, one after the other; what each did."""
    out = []
    limits = session.Limits(total=args.session_seconds)
    limits.phases["other"] = args.hang_seconds
    for n, group in enumerate(session.sessions(chosen_jobs, args.places_per_session), 1):
        names = [p["name"] for p in group]
        this = dict(job, places=group, walks=[w for w in walk_list if w["place"] in names],
                    materials=job["materials"] and n == 1,
                    heartbeat=str(folder / f"heartbeat_{n}.json"),
                    result=str(folder / f"result_{n}.json"))
        job_path = folder / f"job_{n}.json"
        command = session.editor_command(m, v.uproject, driver_dir / "ue_driver.py",
                                         args.offscreen)
        if args.dry_run:
            print("  " + " ".join(command) + f"   ({session.JOB_VARIABLE}={job_path})")
            continue
        job_path.write_text(json.dumps(this, indent=2), encoding="utf-8")
        outcome = session.run_watched(command, job_path, this["heartbeat"],
                                      folder / "logs" / f"editor_stdout_{n}.log", limits)
        log = Path(job["editorLog"])
        kept = folder / "logs" / f"editor_{n}.log"
        if log.is_file():
            shutil.copyfile(log, kept)
        result_path = Path(this["result"])
        out.append({"places": names, "outcome": asdict(outcome), "log": str(kept),
                    "result": json.loads(result_path.read_text(encoding="utf-8"))
                    if result_path.is_file() else None,
                    "scan": ue_log.scan_editor(ue_log.read_text(kept)) if kept.is_file()
                    else None})
    return out


def run_version(args, m, ref, label, chosen, asked_walks, run_folder, runner):
    entry = {"label": label, "ref": ref, "sessions": []}
    try:
        v = workspace.prepare(m, ref, label, runner, fresh=args.fresh)
    except workspace.WorkspaceError as error:
        entry["error"] = str(error)
        return entry
    entry.update(sha=v.sha, build=v.build, cached=not v.built_now, features=v.features)
    folder = run_folder / label
    log = folder / "logs" / "probe.log"
    if not args.dry_run:
        (folder / "logs").mkdir(parents=True, exist_ok=True)
        ubt = v.cache / "logs" / "ubt.log"
        if ubt.is_file():
            shutil.copyfile(ubt, folder / "logs" / "ubt.log")
    planet_file = None
    if any(use_planet(p, args.planet) for p in chosen):
        choice = planet_module.choose(m, v, runner, folder / "logs" / "planet.log")
        planet_file = planet_module.install(choice, v, m, dry=args.dry_run)
        entry["planet"] = {"source": str(choice.path), "origin": choice.origin,
                           "checksum": choice.checksum, "installed": str(planet_file)}
    jobs, entry["probes"] = place_jobs(runner, v, m, chosen, args.planet, planet_file, log)
    by_name = {p.name: p for p in chosen}
    walk_list, entry["profiles"] = walk_jobs(runner, v, m, asked_walks, by_name, args.planet,
                                             planet_file, log)
    job = base_job(args, v, m, folder, planet_file, parse_sun(args.sun, m))
    entry["sessions"] = run_sessions(args, v, m, job, jobs, walk_list, folder,
                                     run_folder / "driver")
    return entry


def cmd_run(args):
    m = machine_module.load_machine(args.machine)
    chosen = places_module.select(places_module.load_places(args.places_file), args.places)
    asked_walks = [walks_module.parse_walk(w) for w in args.walk]
    names = {p.name for p in chosen}
    for w in asked_walks:
        if w["place"] not in names:
            raise SystemExit(f"--walk {w['place']}: the place is not among --places")
    refs = [(args.before, "avant")] + ([(args.after, "apres")] if args.after else [])
    if len(refs) == 1:
        refs = [(args.before, "version")]
    date = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    title = " vs ".join(r for r, _ in refs)
    run_folder = m.work_root / "runs" / f"{date}_{'_vs_'.join(slug(r) for r, _ in refs)}"
    runner = workspace.Runner(dry=args.dry_run)
    if not args.dry_run:
        (run_folder / "driver").mkdir(parents=True, exist_ok=True)
        for name in DRIVER_FILES:
            shutil.copyfile(HERE / name, run_folder / "driver" / name)
    versions = []
    for ref, label in refs:
        print(f"{label} : {ref}")
        versions.append(run_version(args, m, ref, label, chosen, asked_walks, run_folder,
                                    runner))
        if args.dry_run:
            for command in runner.commands:
                print("  " + " ".join(command))
            runner.commands.clear()
    if args.dry_run:
        return 0
    run = {"title": title, "date": date, "folder": str(run_folder), "versions": versions,
           "places": [p.name for p in chosen], "walks": args.walk}
    print(report.write_report(run))
    return 0


def cmd_probe(args):
    import subprocess
    tool = args.probe or str(places_module.REPOSITORY / "build" / "terrain_core" /
                             "terrain_probe")
    chosen = places_module.select(places_module.load_places(args.places_file), args.names)
    command = [tool] + (["--planet", args.planet] if args.planet else []) + (
        ["--json"] if args.json else [])
    for p in chosen:
        command += [str(p.x), str(p.z)]
    print("# " + ", ".join(p.name for p in chosen))
    return subprocess.run(command).returncode


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        return cmd_run(args) if args.command == "run" else cmd_probe(args)
    except (machine_module.MachineError, places_module.PlacesError,
            planet_module.PlanetError, probe_module.ProbeError) as error:
        print(f"verify: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
