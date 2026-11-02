"""The landings of a verification on the side of the launcher: Terrain.Map.Go <place> of the dev
map (agent 35), the path of a click, driven in Unreal by ue_landings.py. Here: which places land
(those measured with the planet file are skipped by a run without it), the core probed under the
feet of the pawn or the eye of the view, what each landing gave, what fails, and the landings
whose outcome differs between the two versions (the world is deterministic: the same click gives
the same outcome)."""

import probe as probe_module

RADIUS = 50.0          # Terrain.PawnRadius of the plugin, when its messages do not say it
FEET_TOLERANCE = 1.0   # m between the feet of the pawn and the ground of the core under them
EYE_ABOVE_WATER, EYE_TOLERANCE = 1.8, 0.5  # the view of the tools over the water of the core
SKIPPED_PLANET = "measured with the planet file, the run has none"
SKIPPED_TEXT = {SKIPPED_PLANET: "mesuré avec le fichier planète, et ce run n'en a pas "
                                "(--planet no) : sauté, ce n'est pas une panne"}


def chosen(text, places):
    """The names of --landings ("all": every place of --places), each among --places."""
    names = [p.name for p in places]
    if text in ("", None):
        return []
    asked = names if text == "all" else [n.strip() for n in text.split(",") if n.strip()]
    for name in asked:
        if name not in names:
            raise SystemExit(f"--landings {name}: the place is not among --places")
    return asked


def jobs(asked, places, uses_planet):
    """What the driver gets: each place, skipped when it was measured with the planet file and
    the run has none for it (the plugin would only warn; the landing would prove nothing)."""
    by_name = {p.name: p for p in places}
    return [{"place": n, "skip": SKIPPED_PLANET if by_name[n].planet and not uses_planet(
        by_name[n]) else None} for n in asked]


def entries(v):
    return [e for s in v.get("sessions", []) for e in (s.get("result") or {}).get("landings", [])]


def cores(runner, version, m, sessions, uses_planet_of, planet_file, log):
    """The core under each landing of the sessions of a version (workspace.Version):
    {place: {"ground", "waterLevel"}} at the feet of the pawn or under the eye of the view."""
    out = {}
    for e in entries({"sessions": sessions}):
        point = e.get("feet") or e.get("eye")
        if not point:
            continue
        try:
            (column,) = probe_module.columns(runner, version, m, [(point[0], point[1])],
                                             planet_file if uses_planet_of(e["place"]) else None,
                                             log)
        except (probe_module.ProbeError, ValueError, OSError):
            continue
        out[e["place"]] = {"ground": column.ground, "waterLevel": column.level}
    return out


def outcome(e):
    """What a landing gave, in words: what the two versions must share."""
    if e.get("unsupported"):
        return "impossible (pas de Terrain.Map.Go)"
    if e.get("skipped"):
        return "sauté"
    return {"none": "sans issue", "refused": "refusé",
            "water": "vue des outils au-dessus de l'eau",
            "fallback": "vue des outils au-dessus de l'eau (repli : aucun point ne tient le "
                        "pion)",
            "check": "pion du jeu posé près du sol sec" if e.get("decision") == "shallow"
            else "pion du jeu posé"}.get(e.get("end"), "?")


def _dry(v, place):
    probe = (v.get("probes") or {}).get(place)
    return probe is not None and probe.get("waterLevel") is None


def failures_of(v, e):
    """Why a landing failed (exit code 1), or []."""
    check, radius = e.get("check"), e.get("radius") or RADIUS
    if e.get("unsupported"):
        return ["la version n'a pas Terrain.Map.Go : atterrissage impossible"]
    if e.get("skipped"):
        return []
    if e.get("end") == "none":
        return [f"aucune issue en {e.get('seconds') or 0:.0f} s"]
    if e.get("end") == "refused":
        return [e.get("message") or "refusé"]
    if e.get("end") in ("water", "fallback") and _dry(v, e["place"]):
        return ["lieu sec selon le core, mais la vue des outils est restée au-dessus de l'eau"]
    if not check:
        return []
    out = [] if check["ok"] else [check["line"]]
    if check["who"] != "the pawn of the game":
        out.append("la ligne de contrôle parle de la vue des outils, pas du pion du jeu")
    if check["away"] > radius:
        out.append(f"pion à {check['away']:.1f} m du clic, plus que le rayon de {radius:.0f} m")
    return out


def notices_of(v, e):
    """What deserves a look: the feet or the eye against the core probed there."""
    core = (v.get("landingCores") or {}).get(e["place"])
    if not core:
        return []
    if e.get("feet") and core.get("ground") is not None:
        gap = e["feet"][2] - core["ground"]
        if abs(gap) > FEET_TOLERANCE:
            return [f"pieds à {e['feet'][2]:.2f} m, sol du core {core['ground']:.2f} m sous le "
                    f"pion (écart {gap:+.2f} m)"]
    if e.get("eye") and core.get("waterLevel") is not None:
        gap = e["eye"][2] - core["waterLevel"] - EYE_ABOVE_WATER
        if abs(gap) > EYE_TOLERANCE:
            return [f"œil à {e['eye'][2]:.2f} m, eau du core {core['waterLevel']:.2f} m "
                    f"(écart {gap:+.2f} m aux 1,8 m attendus)"]
    return []


def failures(v):
    return [f"ATTENTION ({v['label']}) : atterrissage {e['place']} : {why}"
            for e in entries(v) for why in failures_of(v, e)]


def notices(v):
    return [f"AVERTISSEMENT ({v['label']}) : atterrissage {e['place']} : {why}"
            for e in entries(v) for why in notices_of(v, e)]


def differences(run):
    """The landings whose outcome differs between the two versions."""
    if len(run["versions"]) != 2:
        return []
    before, after = ({e["place"]: outcome(e) for e in entries(v)} for v in run["versions"])
    return [f"ATTENTION : atterrissage {place} : issue différente entre avant ({before[place]}) "
            f"et après ({after[place]})" for place in before
            if place in after and before[place] != after[place] and "sauté" not in
            (before[place], after[place])]


def _details(v, e):
    check, core = e.get("check"), (v.get("landingCores") or {}).get(e["place"]) or {}
    if e.get("skipped") and not e.get("unsupported"):
        return f" : {SKIPPED_TEXT.get(e['skipped'], e['skipped'])}"
    if check:
        under = f", sol du core {core['ground']:.2f} m" if core.get("ground") is not None else ""
        return (f" : {check['mode']}, {check['speed']:.2f} m/s, pieds à {check['feet']:.2f} m"
                f"{under}, {check['water']}, à {check['away']:.1f} m du clic : "
                f"{'OK' if check['ok'] else 'PAS OK'}")
    if e.get("eye"):
        water = (f", eau du core {core['waterLevel']:.2f} m" if core.get("waterLevel") is not None
                 else "")
        return f" : œil à {e['eye'][2]:.2f} m{water}"
    return f" : {e['message']}" if e.get("message") else ""


def lines(v):
    out = []
    for e in entries(v):
        took = f" ({e['seconds']:.0f} s)" if e.get("seconds") is not None else ""
        out.append(f"    {e['place']} : {outcome(e)}{_details(v, e)}{took}")
        if e.get("planetWarning"):
            out.append(f"      avertissement du plugin (pas une panne) : {e['planetWarning']}")
    return out
