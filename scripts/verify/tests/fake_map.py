"""Terrain.Map.Go <place> of the plugin (agent 35, TerrainDevMapLanding.cpp) on the stand-in
module unreal (fake_unreal.py): the landing decided on the thread of the map (0.3 s), the
teleport of the tools and its load (3 s), the pawn of the game given back near the view, the
line of the check a second later; over water the view of the tools 1.8 m above the water and
no line of the check. A new command replaces the one running, as in the plugin."""

import fake_unreal
from fake_unreal import Vector

DECIDE, LOAD, CHECK = 0.3, 3.0, 1.0
WATER_TEXT = ("over water (no dry ground within 50 m): the view of the tools 1.8 m above the "
              "water; Terrain.Pawn over land")


class MapGo:
    """outcomes: {place: {"kind": land | shallow | water | fallback | refused | silent |
    notready, "away": m from the click, "feet": altitude, "ok": False for NOT walking,
    "warning": the warning of the planet file}}; land by default, at the click. ready_after:
    the seconds after the start of a play before the world and its dev map are ready ("Terrain
    Dev Map: started"); a command sooner is refused with "no world playing", as in the plugin."""

    def __init__(self, fake, places, outcomes=None, ready_after=0.5, determinism_after=None):
        self.fake, self.places = fake, {p["name"]: p for p in places}
        self.outcomes, self.ready_after = outcomes or {}, ready_after
        self.determinism_after, self.determinism_at = determinism_after, None
        self.events, self.sent = [], []  # (time, action); (name, log size at the command)
        self.game, self.ready_at = None, None
        fake.console_hooks.append(self.console)
        fake.tick_hooks.append(self.tick)

    def at(self, delay, action):
        self.events.append((self.fake.time + delay, action))

    def tick(self):
        if self.fake.game is not self.game:  # a new play: its world builds first
            self.game, self.ready_at = self.fake.game, self.fake.time + self.ready_after
            if self.game is not None and self.determinism_after is not None:
                self.determinism_at = self.fake.time + self.determinism_after
        if self.determinism_at is not None and self.fake.time >= self.determinism_at:
            self.determinism_at = None  # the self-check of the plugin, on its thread
            self.fake._log("Terrain World: the heights and tiles of the default world match "
                           "terrain_core bit for bit (checked on a thread in 9.10 s)")
        if self.ready_at is not None and self.fake.time >= self.ready_at:
            self.ready_at = None
            self.fake._log("Terrain Dev Map: started (1 thread(s), 192 tiles kept)")
        for due, action in list(self.events):
            if self.fake.time >= due and (due, action) in self.events:
                self.events.remove((due, action))
                action()

    def origin(self):
        props = self.fake.game.terrain.properties
        for p in self.places.values():
            if (p["x"], p["z"]) == (props["core_origin_x"], props["core_origin_z"]):
                return p["x"], p["z"], p["ground"]
        return props["core_origin_x"], props["core_origin_z"], 0.0

    def console(self, command):
        if not command.startswith("Terrain.Map.Go ") or self.fake.game is None:
            return
        name = command.split(" ", 1)[1]
        self.sent.append((name, self.fake.log_path.stat().st_size))
        spec = dict({"kind": "land", "away": 0.0, "ok": True}, **self.outcomes.get(name, {}))
        if self.ready_at is not None or spec["kind"] == "notready":  # its world still builds
            self.fake._log("Warning: Terrain.Map.Go: no world playing (or no dev map)")
            return
        self.events.clear()  # the new click replaces the one running
        if spec.get("warning"):
            self.fake._log(f"Warning: Terrain Dev Map: {name} {spec['warning']}")
        place = self.places[name]
        if spec["kind"] == "refused":
            self.at(DECIDE, lambda: self.fake._log(
                f"Terrain Dev Map: No landing for {name}: the columns could not be read"))
        elif spec["kind"] != "silent":
            self.at(DECIDE, lambda: self.decide(name, place, spec))

    def decide(self, name, place, spec):
        x, z = place["x"] + spec["away"], place["z"]
        self.fake.begin_load("teleport", x, z, LOAD)
        kind = spec["kind"]
        if kind == "water":
            said = WATER_TEXT
        elif kind in ("shallow", "fallback"):
            said = (f"shallow water: the pawn of the game goes down near the dry ground at "
                    f"x {x:.2f}, z {z:.2f}")
        else:
            said = f"the pawn of the game goes down near x {x:.2f}, z {z:.2f}"
        self.fake._log(f"Terrain Dev Map: {name}: {said}")
        x0, z0, y0 = self.origin()
        if kind in ("water", "fallback"):
            eye = (place["waterLevel"] or 0.0) + 1.8
            point = Vector(100.0 * (place["x"] - x0), 100.0 * (place["z"] - z0),
                           100.0 * (eye - y0))
            self.at(LOAD, lambda: setattr(self.fake, "camera_point", point))
            if kind == "fallback":
                back = "no spot near the dry ground holds the pawn: back " + WATER_TEXT
                self.at(LOAD + 0.2, lambda: self.fake._log(f"Terrain Dev Map: {back}"))
            return
        feet = spec.get("feet", place["ground"])
        self.at(LOAD + 0.2, lambda: self.give_pawn(x - x0, z - z0, feet - y0))
        self.at(LOAD + 0.2 + CHECK, lambda: self.check(spec, feet))

    def give_pawn(self, x, z, feet):
        pawn = fake_unreal.Pawn(self.fake)
        pawn.location = Vector(100.0 * x, 100.0 * z, 100.0 * feet + 90.0)
        self.fake.game.pawn = pawn

    def check(self, spec, feet):
        mode, speed, verdict, level = "Walking", 0.0, "OK", ""
        if not spec["ok"]:
            mode, speed, verdict, level = ("Falling", 2.5, "NOT walking still above the water",
                                           "Warning: ")
        self.fake._log(f"{level}Terrain Dev Map: 1 s after the click: the pawn of the game, "
                       f"{mode}, {speed:.2f} m/s, feet at {feet:.2f} m (no water within 3 m), "
                       f"{spec['away']:.1f} m from the click: {verdict}")
