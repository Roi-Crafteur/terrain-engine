"""The check of the sign of the pitch of the driver (ue_driver.py): once a session, at the
first dry place that looks level or down, the same view pitched up then down by 30 degrees, the
pitch the camera manager says each has, two small images whose shares of sky the launcher
compares."""

from ue_walk import Wait

PITCH_CHECK = -30.0  # degrees: the check view looks down by this much
# A place pitched up by more than this looks at something above the eye (a wall, a fall): its
# view pitched up by 30 degrees shows no sky, no place for the check of the pitch.
PITCH_CHECK_MAX_PITCH = 10.0


class PitchSteps:
    """The steps of the check, for Driver (self.api, self.result, self.aim, self.shot)."""

    def check_pitch(self, world, place, camera):
        """The check of the sign of the pitch, at the place chosen for it: the same view pitched
        up then down by 30 degrees, the pitch the camera manager says each has, and two small
        images whose shares of sky the launcher compares (the water of a fall reflects the sky
        and its foam is white: only the comparison tells)."""
        check = {"place": place["name"], "requested": PITCH_CHECK, "seen": None, "file": None,
                 "upRequested": -PITCH_CHECK, "upSeen": None, "upFile": None}
        self.result["pitchCheck"] = check
        for pitch, prefix in ((-PITCH_CHECK, "up"), (PITCH_CHECK, "")):
            self.aim(world, place, pitch, camera, move=False)
            yield Wait(2.0)
            seen = self.api.camera_rotation(world)
            stem = f"_pitch_{'up' if prefix else 'down'}_{place['name']}"
            shot, _ = yield from self.shot(world, stem.lstrip("_"), 480, 270, stem)
            check["upSeen" if prefix else "seen"] = None if seen is None else seen[0]
            check["upFile" if prefix else "file"] = shot

    @staticmethod
    def pitch_place(places):
        """The place of the session where the pitch is checked: the first one without water under
        the eye (water reflects the sky, foam is white) that looks level or down (pitched up, it
        looks at a wall or a fall: no sky at +30 degrees); None when there is none."""
        for p in places:
            if p.get("waterLevel") is None and p.get("pitch", 0.0) <= PITCH_CHECK_MAX_PITCH:
                return p["name"]
        return None
