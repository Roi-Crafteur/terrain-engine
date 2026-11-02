"""The landings of the driver (ue_driver.py): Terrain.Map.Go <place> of the dev map (agent 35),
the very path of a click on the map, one place at a time. Only the lines written after its
command count (an older line OK of the log is no landing of this one), and the next command
waits for the end of the previous one (a new command would replace the one running). The end:
the line of the check a second after the pawn of the game is given back, or the view of the
tools left above the water (no line of the check then), or a refusal. Then the feet of the pawn,
or the eye of the view, in the frame of the core, for the launcher to probe the core there.
Several landings in a row without an end stop the session: Unreal never waits for them all."""

import ue_log
from ue_walk import Until, Wait


class SessionStopped(Exception):
    """Too many landings in a row without an end: the session closes Unreal."""

    def __init__(self, misses, after):
        super().__init__(f"{misses} landings in a row without an end (after {after}): the "
                         f"session stops")
        self.misses, self.after = misses, after


class LandingSteps:
    """The steps of the landings, for Driver (self.api, self.job, self.result, self.origin...)."""

    def landings_of(self, world, place):
        for landing in [w for w in self.job.get("landings", []) if w["place"] == place["name"]]:
            self.set_phase("landing")
            entry = {"place": place["name"], "end": None, "seconds": None,
                     "skipped": landing.get("skip"), "unsupported": False}
            self.result["landings"].append(entry)
            if entry["skipped"]:
                continue  # measured with the planet file, and the run has none
            if not (self.dev_commands() and self.result["features"].get("mapGo")):
                entry.update(unsupported=True, skipped="no Terrain.Map.Go in this version")
                continue
            yield from self.land(world, place, entry)
            self.save_result()

    def land(self, world, place, entry):
        offset, started = self.log_size(), self.clock()
        self.api.console(world, f"Terrain.Map.Go {place['name']}")
        until = Until(lambda: self.landing_seen(offset, place["name"]),
                      self.job.get("landingTimeout", 90.0), "the end of the landing")
        yield until
        entry["seconds"] = round(self.clock() - started, 1)
        if until.timed_out:
            entry["end"] = "none"
            yield from self.missed(place)
            return
        entry.update(self.landing)
        if entry["end"] == "notReady":  # sent too early: a problem of the launcher, a miss
            yield from self.missed(place)
            return
        self.landing_misses = 0
        if entry["end"] in ("water", "fallback"):
            # The eye goes above the water once the ground of the point is read: after the
            # report of the load of its teleport.
            yield Until(lambda: self.load_reported(offset),
                        self.job.get("landingLoadTimeout", 60.0), "the load of the landing")
            yield Wait(0.5)
            entry["eye"] = self.core_point(self.api.camera_location(world))
        elif entry["end"] == "check":
            pawn = self.api.pawn(world)
            if pawn is not None:
                x, y, z = self.api.location(pawn)
                entry["feet"] = self.core_point((x, y, z - self.api.half_height(pawn)))
        entry["seconds"] = round(self.clock() - started, 1)

    def missed(self, place):
        """A landing without an end, or sent before the world was ready: two in a row stop the
        session."""
        self.landing_misses += 1
        if self.landing_misses >= self.job.get("landingMisses", 2):
            raise SessionStopped(self.landing_misses, place["name"])
        yield from ()

    def landing_seen(self, offset, name):
        self.landing = ue_log.landing_end(self.log_text(offset), name)
        return self.landing is not None

    def core_point(self, point):
        """A point of Unreal (cm from the origin of the frame of the play) in the core: [x, z,
        altitude] in meters, or None."""
        if point is None or self.origin is None:
            return None
        x0, z0, y0 = self.origin
        return [x0 + point[0] / 100.0, z0 + point[1] / 100.0, y0 + point[2] / 100.0]
