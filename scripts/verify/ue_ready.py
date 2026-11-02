"""The world of a play ready, for the driver (ue_driver.py). In Play In Editor the pawn is there
from the first frame, while the Terrain World still builds on its thread: a command sent then
is refused (Terrain.Map.Go: "no world playing (or no dev map)"; Terrain.Goto: no teleport, so no
load of it to wait for). So before the first view or landing of a play: its first load reported
(agent 38) and, when the session lands, the dev map started (agent 35), as far as the version
logs them; a clear error after readyTimeout. And before the session ends: the line of the
self-check of the determinism of the plugin, which a thread writes a few seconds after the start
(a session closed sooner leaves "déterminisme : ?")."""

import ue_log
from ue_walk import Until

MAP_STARTED = "Terrain Dev Map: started ("
DETERMINISM = ("match terrain_core bit for bit", "does not generate the same world")


class ReadySteps:
    """The waits of the world, for Driver (self.api, self.job, self.result, self.log_text...)."""

    def ready_signals(self):
        f = self.result["features"]
        wanted = []
        if f.get("loadReport"):
            wanted.append(("the first load of the play reported", lambda text: ue_log.load_ended(
                text, f.get("loadStart", False)) is not None))
        if f.get("mapStarted") and self.job.get("landings"):
            wanted.append(("the dev map started", lambda text: MAP_STARTED in text))
        return wanted

    def world_ready(self, offset):
        """offset: the size of the log of the editor before the start of the play."""
        wanted = self.ready_signals()
        if not wanted:
            return  # an older plugin: the delays of the views (--settle, --first-settle)
        self.set_phase("world-ready")
        timeout, started = self.job.get("readyTimeout", 180.0), self.clock()
        until = Until(lambda: all(test(self.log_text(offset)) for _, test in wanted), timeout,
                      "the world of the play ready")
        yield until
        self.result.setdefault("ready", []).append(round(self.clock() - started, 1))
        if until.timed_out:
            text = self.log_text(offset)
            missing = [what for what, test in wanted if not test(text)]
            raise RuntimeError(f"the world of the play is not ready after {timeout:.0f} s: "
                               f"{', '.join(missing)} not seen in the log of the editor")

    def await_determinism(self):
        """The line of the check of the determinism, at most determinismWait seconds more."""
        if not self.result["features"].get("determinism"):
            return
        until = Until(lambda: any(line in self.log_text(0) for line in DETERMINISM),
                      self.job.get("determinismWait", 60.0), "the check of the determinism")
        yield until
