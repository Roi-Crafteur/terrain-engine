"""The summary of a verification: resume.txt (in French, for the people who read it), the contact
sheet (planche.png and planche.html) and the pixel differences of each view.

A run is a dict (written by verify.py to run.json):
  {"title", "date", "versions": [{"label", "ref", "sha", "build", "cached", "planet",
   "sessions": [{"outcome", "result", "log"}], "probes": {place: frame}, "profiles": {...}}],
   "places": [names], "walks": [...], "folder": run folder}
"""

import json
import re
from pathlib import Path

import pixels
import pngio
import sheet
import walks as walks_module

PITCH_TOLERANCE = 5.0  # degrees between the pitch asked and the one the camera shows


def _read(path):
    if not path or not Path(path).is_file():
        return None
    try:
        return pngio.read_png(path)
    except (pngio.PngError, OSError):
        return None


def _views(version):
    """{place: (file, repeat)} of all the sessions of a version."""
    out = {}
    for s in version.get("sessions", []):
        for v in (s.get("result") or {}).get("views", []):
            out[v["place"]] = (v.get("file"), v.get("repeat"))
    return out


def _sky(path):
    image = _read(path)
    return pixels.sky_share(image) if image is not None else None


def pitch_warning(version):
    """A notice for the top of the summary when the check of the pitch looks wrong, else None:
    the camera says it pitches the other way, or the view pitched down by 30 degrees does not
    show clearly less sky than the same view pitched up (water and foam fool one image alone)."""
    for s in version.get("sessions", []):
        check = (s.get("result") or {}).get("pitchCheck")
        if not check:
            continue
        seen, asked = check.get("seen"), check.get("requested", -30.0)
        check["sky"], check["upSky"] = _sky(check.get("file")), _sky(check.get("upFile"))
        down, up = check["sky"], check["upSky"]
        label = version["label"]
        if seen is not None and seen * asked < 0:
            return (f"AVERTISSEMENT ({label}) : inclinaison inversée ? la caméra dit {seen:.1f}° "
                    f"pour {asked:.0f}° demandés ({check.get('place')})")
        if seen is not None and abs(seen - asked) > PITCH_TOLERANCE:
            return (f"AVERTISSEMENT ({label}) : la caméra n'a pas l'inclinaison demandée "
                    f"({asked:.0f}° demandés, {seen:.1f}° vus) : la caméra du pion l'écrase ?")
        if down is not None and up is not None and down > up - pixels.SKY_MARGIN:
            return (f"AVERTISSEMENT ({label}) : inclinaison inversée ? ciel {_pct(down)} vers le "
                    f"bas contre {_pct(up)} vers le haut ({check.get('place')})")
    return None


def pitch_line(version):
    for s in version.get("sessions", []):
        check = (s.get("result") or {}).get("pitchCheck")
        if check:
            return (f"  contrôle de l'inclinaison à {check.get('place')} : la caméra dit "
                    f"{_deg(check.get('seen'))} pour -30° et {_deg(check.get('upSeen'))} pour "
                    f"+30° ; ciel {_pct(check.get('sky'))} vers le bas, "
                    f"{_pct(check.get('upSky'))} vers le haut")
    return "  contrôle de l'inclinaison : aucun"


def difference_text(d):
    return (f"moyenne {d.mean:.2f}/255, {100.0 * d.changed:.2f} % des pixels à plus de "
            f"{d.threshold}, au plus {d.largest}")


def _deg(value):
    return "rien" if value is None else f"{value:.1f}°"


def _pct(value):
    return "?" if value is None else f"{100.0 * value:.0f} %"


def _session_warnings(v, s, n):
    """The alerts of one session: killed, ended before the driver, without its result, the
    problems its log shows."""
    label = v["label"]
    outcome, scan, result = s.get("outcome") or {}, s.get("scan") or {}, s.get("result")
    beat = outcome.get("last_beat") or {}
    out = []
    if outcome.get("killed"):
        out.append(f"ATTENTION ({label}) : session {n} tuée : {outcome.get('reason')}")
    elif not beat.get("done"):
        out.append(f"ATTENTION ({label}) : session {n} : l'éditeur s'est fermé avant la fin du "
                   f"pilote (sortie {outcome.get('exit_code')}, dernier battement : phase "
                   f"{beat.get('phase', 'aucun')}, n {beat.get('n', 0)})")
    if result is None:
        out.append(f"ATTENTION ({label}) : session {n} sans résultat (result_{n}.json absent)")
    elif not result.get("done"):
        out.append(f"ATTENTION ({label}) : session {n} : le pilote n'a pas fini son résultat")
    missing = (result or {}).get("missingApi", [])
    if missing:
        out.append(f"ATTENTION ({label}) : session {n} : API d'Unreal manquante : "
                   f"{', '.join(missing)}")
    for error in (result or {}).get("errors", [])[:2]:
        last = (str(error).strip().splitlines() or [""])[-1]
        out.append(f"ATTENTION ({label}) : session {n} : erreur du pilote : {last[:160]}")
    if scan.get("determinism") is False:
        out.append(f"ATTENTION ({label}) : le plugin ne génère pas le même monde que "
                   "terrain_core")
    if scan.get("crash"):
        out.append(f"ATTENTION ({label}) : plantage : {scan['crash'][0]}")
    return out


def _missing_outputs(v, run):
    """A view or a walk asked of a version that has no file, unless its session already failed
    as a whole."""
    out = []
    views = _views(v)
    for s in v.get("sessions", []):
        result = s.get("result")
        if result is None:
            continue
        for place in s.get("places", []):
            file = (views.get(place) or (None,))[0]
            if not file or not Path(file).is_file():
                out.append(f"ATTENTION ({v['label']}) : vue absente : {place}")
        asked = [w for w in run.get("walkPlaces", []) if w in s.get("places", [])]
        done = {w["place"] for w in result.get("walks", []) if w.get("file")}
        for place in asked:
            if place not in done:
                out.append(f"ATTENTION ({v['label']}) : marche absente : {place}")
    return out


def warnings(run):
    """The failures of a run (exit code 1): a version, a session, a view or a walk that failed,
    a crash, the project of the user changed."""
    out = []
    for v in run["versions"]:
        if v.get("error"):
            out.append(f"ATTENTION ({v['label']}) : {v['error'].splitlines()[0]}")
        for n, s in enumerate(v.get("sessions", []), 1):
            out += _session_warnings(v, s, n)
        out += _missing_outputs(v, run)
    safety = run.get("safety")
    if safety is not None and not _untouched(safety):
        changed = safety["changed"] + safety["added"] + safety["removed"]
        planets = [p for p, same in safety["planets"].items() if not same]
        out.append("ATTENTION : le projet de l'utilisateur a changé pendant le run : "
                   f"{', '.join((changed + planets)[:10])}")
    return out


def notices(run):
    """What deserves a look without being a failure (exit code 0): the check of the pitch, a
    planet that Unreal built again."""
    out = []
    for v in run["versions"]:
        line = pitch_warning(v)
        if line:
            out.append(line)
        if any((s.get("scan") or {}).get("planetBuilt") for s in v.get("sessions", [])):
            out.append(f"AVERTISSEMENT ({v['label']}) : Unreal a reconstruit la planète "
                       "(fichier copié refusé ?)")
    return out


def _untouched(safety):
    return (not safety["changed"] and not safety["added"] and not safety["removed"] and
            all(safety["planets"].values()))


def safety_line(run):
    safety = run.get("safety")
    if safety is None:
        return "Projet de l'utilisateur : non vérifié."
    if _untouched(safety):
        return (f"Projet de l'utilisateur : intact ({safety['files']} fichiers, tailles et dates "
                f"identiques ; planètes identiques, SHA256) ; Binaries, Intermediate, Saved et "
                f"DDC non comptés.")
    return (f"Projet de l'utilisateur : {len(safety['changed'])} fichier(s) changé(s), "
            f"{len(safety['added'])} ajouté(s), {len(safety['removed'])} retiré(s).")


def _session_lines(v):
    lines = []
    for n, s in enumerate(v.get("sessions", []), 1):
        outcome, scan, result = s.get("outcome") or {}, s.get("scan") or {}, s.get("result") or {}
        state = ("tuée : " + str(outcome.get("reason"))) if outcome.get("killed") else (
            f"sortie {outcome.get('exit_code')}")
        lines.append(f"  session {n} : {state}, {outcome.get('seconds', 0):.0f} s, "
                     f"lieux {', '.join(s.get('places', []))}")
        scripts = result.get("materials", [])
        bad = [m["script"] for m in scripts if not m["ok"]]
        if scripts:
            lines.append(f"    scripts de matériaux : {len(scripts) - len(bad)}/{len(scripts)} ok"
                         + (f" ; en erreur : {', '.join(bad)}" if bad else ""))
        determinism = {True: "ok", False: "FAUX"}.get(scan.get("determinism"), "?")
        logged = len(scan.get("pythonErrors", []))
        driver = len(result.get("errors", []))
        lines.append(f"    erreurs de shader : {len(scan.get('shaderErrors', []))} ; erreurs "
                     f"Python : {logged + driver + len(bad)} (journal {logged}, pilote "
                     f"{driver}, scripts {len(bad)}) ; chargements rapportés : "
                     f"{len(scan.get('loadReports', []))} ; déterminisme : {determinism}")
        if result.get("reentered"):
            lines.append(f"    ticks venus pendant une étape (ignorés) : {result['reentered']}")
        for line in (scan.get("shaderErrors", []) + scan.get("terrainProblems", []))[:5]:
            lines.append(f"      {line[:160]}")
        for name in ("missingApi", "errors"):
            for item in result.get(name, [])[:3]:
                last = (str(item).strip().splitlines() or [""])[-1]
                lines.append(f"    {name} : {last[:160]}")
    return lines


def _noise_lines(v):
    lines = []
    for place, (first, repeat) in _views(v).items():
        a, b = _read(first), _read(repeat)
        if a is None or b is None:
            lines.append(f"    {place} : vue {'absente' if a is None else 'sans 2e prise'}")
            continue
        try:
            lines.append(f"    {place} : {difference_text(pixels.compare(a, b))}")
        except pixels.SizeError as error:
            lines.append(f"    {place} : tailles différentes ({error})")
    return lines


def _walk_lines(v, run):
    lines = []
    for s in v.get("sessions", []):
        for w in (s.get("result") or {}).get("walks", []):
            if not w.get("file") or not Path(w["file"]).is_file():
                lines.append(f"    {w['place']} : pas de journal ({w.get('reason')})")
                continue
            samples = walks_module.read_walk(w["file"])
            sm = walks_module.summarize(samples, w.get("reason"),
                                        v.get("profiles", {}).get(w["place"]),
                                        core=v.get("walkCores", {}).get(w["place"]),
                                        heading=w["heading"])
            lines += _walk_text(w, sm)
    return lines


def _z(value):
    return "?" if value is None else f"{value:.2f} m"


def _walk_text(w, sm):
    lines = [f"    {w['place']} cap {w['heading']:.0f}° sur {w['distance']:.0f} m : arrêt "
             f"« {sm['reason']} » à {sm['distance']:.1f} m en {sm['seconds']:.1f} s, "
             f"{sm['falls']} chute(s), plus grande descente {sm['largestDrop']:.2f} m, plus "
             f"grande marche au sol {sm['largestStep']:.2f} m, sous le sol du core : "
             f"{sm['belowCoreGround']} échantillon(s)",
             f"      pieds au départ {_z(sm['start'])} (core {_z(sm.get('coreStart'))}), à la fin "
             f"{_z(sm['end'])} (core {_z(sm.get('coreEnd'))})"]
    followed, deviation = sm.get("headingFollowed"), sm.get("deviation")
    if followed is None:
        lines.append("      le pion n'a pas avancé")
    elif deviation is not None and deviation > 20.0:
        lines.append(f"      le pion n'a pas suivi le cap : {followed:.0f}° au lieu de "
                     f"{w['heading']:.0f}° (écart {deviation:.0f}°)")
    else:
        lines.append(f"      cap suivi {followed:.0f}°")
    if sm["reason"] == "stuck":
        lines.append(f"      le pion est resté bloqué à {sm['distance']:.1f} m")
    return lines


def version_lines(v, run):
    lines = [f"== {v['label']} : {v['ref']} ({v.get('sha', '?')[:10]})"]
    build = v.get("build") or {}
    lines.append(f"  build : {build.get('result', '?')}"
                 f"{' (cache du commit)' if v.get('cached') else ''}, "
                 f"{build.get('warnings', 0)} avertissement(s)")
    planet = v.get("planet")
    lines.append("  planète : " + ("aucune" if not planet else
                                   f"{planet['source']} ({planet['origin']}), somme "
                                   f"{planet['checksum']}, copiée dans {planet['installed']}"))
    lines += _session_lines(v)
    lines.append(pitch_line(v))
    lines.append("  bruit à monde identique (2 prises à 2 s, jeu en pause) :")
    lines += _noise_lines(v) or ["    (aucune)"]
    lines.append("  marches :")
    lines += _walk_lines(v, run) or ["    (aucune)"]
    lines.append("  sondes du core (sol / eau / œil) :")
    for place, f in v.get("probes", {}).items():
        water = "sec" if f.get("waterLevel") is None else f"eau {f['waterLevel']:.2f} m"
        lines.append(f"    {place} : sol {f['ground']:.2f} m, {water}, œil {f['eye']:.2f} m")
    return lines


def comparison(run):
    """The rows of the sheet and the lines of the differences, one per place."""
    versions = run["versions"]
    views = [_views(v) for v in versions]
    rows, lines = [], []
    for place in run["places"]:
        images = [_read(view.get(place, (None, None))[0]) for view in views]
        links = [view.get(place, (None, None))[0] for view in views]
        caption = ""
        if len(images) == 2:
            a, b = images
            if a is not None and b is not None:
                try:
                    d = pixels.compare(a, b)
                    caption = difference_text(d)
                    images.append(pixels.difference_image(a, b))
                except pixels.SizeError as error:
                    caption = f"tailles différentes ({error})"
                    images.append(None)
            else:
                caption = "vue absente"
                images.append(None)
            lines.append(f"  {place} : {caption}")
        rows.append(sheet.Row(place, caption, images, [_relative(l, run) for l in links]))
    return rows, lines


def _relative(path, run):
    if not path:
        return None
    try:
        return Path(path).resolve().relative_to(Path(run["folder"]).resolve()).as_posix()
    except ValueError:
        return Path(path).as_uri()


def _short(version):
    """The commit of a version in 10 characters (a whole SHA ran over the next column)."""
    sha = version.get("sha") or ""
    return sha[:10] if re.fullmatch(r"[0-9a-f]{7,40}", sha) else version["ref"][:16]


def write_report(run):
    folder = Path(run["folder"])
    rows, diff_lines = comparison(run)
    columns = [f"{v['label']} {_short(v)}" for v in run["versions"]]
    if len(columns) == 2:
        columns.append("ecarts x4")
    sheet.write_sheet(folder / "planche.png", folder / "planche.html", run["title"], columns,
                      rows)
    alerts, advice = warnings(run), notices(run)
    run["alerts"], run["notices"] = alerts, advice
    lines = [f"Vérification du {run['date']} : {run['title']}"]
    lines += (alerts + advice) or ["Aucune alerte."]
    lines += [safety_line(run), ""]
    for v in run["versions"]:
        lines += version_lines(v, run) + [""]
    if len(run["versions"]) == 2:
        lines.append("== écarts de pixels avant / après, par vue :")
        lines += diff_lines
        lines.append("")
    lines.append(f"Planche : {folder / 'planche.png'} (et planche.html)")
    text = "\n".join(lines) + "\n"
    (folder / "resume.txt").write_text(text, encoding="utf-8")
    (folder / "run.json").write_text(json.dumps(run, indent=2, default=str), encoding="utf-8")
    return text
