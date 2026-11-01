"""The test walks of the driver (ue_driver.py), in the Unreal editor: the character of the play
put on the ground of the place (or brought back by Terrain.Pawn of the dev tools), walking along
a heading with add_movement_input until walks.WalkWatch stops it (the first fall, the distance,
stuck, a timeout), its log written (walks.py). Also the waits the steps of the driver yield."""

import math

import ue_log
import walks


class Wait:
    def __init__(self, seconds):
        self.seconds = seconds


class Until:
    def __init__(self, ready, timeout, what):
        self.ready, self.timeout, self.what = ready, timeout, what
        self.timed_out = False


class WalkSteps:
    """The steps of the walks, for Driver (self.api, self.job, self.result, self.origin...)."""

    def walks_of(self, world, place):
        for walk in [w for w in self.job.get("walks", []) if w["place"] == place["name"]]:
            self.set_phase("walk")
            if self.skip_walks:
                self.result["walks"].append({"place": place["name"], "heading": walk["heading"],
                                             "distance": walk["distance"], "file": None,
                                             "reason": "API missing"})
                continue
            pawn = self.api.pawn(world)
            entry = {"place": place["name"], "heading": walk["heading"],
                     "distance": walk["distance"], "file": None, "reason": None}
            if self.dev_commands() and self.result["features"].get("pawn"):
                pawn = yield from self.pawn_back(world, pawn)
                entry["landing"] = self.landing
                if pawn is None:
                    entry["reason"] = "pawn refused"
                    self.result["walks"].append(entry)
                    continue
            else:  # on the ground of the place, the origin of the frame
                try:
                    self.api.set_focus(world, None)  # the tiles around the pawn again
                except Exception:  # noqa: BLE001 - the camera stands over the same place
                    pass
                self.api.teleport(pawn, self.api.vector(0.0, 0.0,
                                                        self.api.half_height(pawn) + 50.0))
                yield Wait(1.0)
            self.api.view_through(world, pawn)
            self.result["walks"].append(entry)
            yield from self.walk(world, pawn, walk, entry)

    def pawn_back(self, world, tools_pawn):
        """The pawn of the game given back by Terrain.Pawn (agent 36), once it stands (its move
        input is ignored while it lands, and the walk forces its input); None when Terrain.Pawn
        refused: the flying pawn of the tools stays (over water, no walkable ground). What it did
        goes in self.landing (ue_log.pawn_landing)."""
        offset = self.log_size()
        self.landing = None
        self.api.console(world, "Terrain.Pawn")
        if self.result["features"].get("pawnLanding"):
            yield Until(lambda: self.pawn_landed(offset), self.job.get("landTimeout", 15.0),
                        "the pawn of the game on the ground")
        else:
            yield Wait(2.0)
        pawn = self.api.pawn(world)
        given = pawn is not None and not self.api.same_actor(pawn, tools_pawn)
        if given or (self.landing or {}).get("state") == "already":
            return pawn
        if self.landing is None or self.landing["state"] != "refused":
            self.landing = {"state": "refused", "message": (self.landing or {}).get(
                "message", "the flying pawn of the tools stayed")}
        return None

    def pawn_landed(self, offset):
        self.landing = ue_log.pawn_landing(self.log_text(offset))
        return self.landing is not None

    def walk(self, world, pawn, walk, entry):
        x0, z0, y0 = self.origin
        heading = math.radians(walk["heading"])
        dx, dz = math.sin(heading), -math.cos(heading)
        self.api.face(world, walk["yaw"])
        half = self.api.half_height(pawn)
        sx, sy, _ = self.api.location(pawn)
        watch = walks.WalkWatch(walk["distance"], timeout=self.job.get("walkTimeout", 120.0))
        samples, started, last = [], self.clock(), -1.0
        while True:
            self.api.walk_input(pawn, dx, dz)
            now = self.clock() - started
            if now - last >= 0.1:
                last = now
                ux, uy, uz = self.api.location(pawn)
                hit = self.api.trace_down(world, ux, uy, uz - half, pawn)
                s = walks.Sample(now, x0 + ux / 100.0, z0 + uy / 100.0,
                                 y0 + (uz - half) / 100.0, self.api.movement_mode(pawn),
                                 None if hit is None else y0 + hit / 100.0,
                                 math.hypot(ux - sx, uy - sy) / 100.0)
                samples.append(s)
                reason = watch.feed(s)
                if reason:
                    entry["reason"] = reason
                    break
            yield Wait(0.0)
        path = self.out / "walks" / f"{walk['place']}.csv"
        path.parent.mkdir(parents=True, exist_ok=True)
        walks.write_walk(path, samples)
        entry["file"] = str(path)
