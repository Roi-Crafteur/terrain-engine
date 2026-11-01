"""The test walks: a character walks from a place along a heading, and stops at the first fall,
at the distance asked, when stuck or after a time. The log of a walk (CSV) holds, every sample,
the time, the position in the core (x, z, the altitude of the feet), the movement mode of the
character and the altitude of the ground under it found by a trace down in Unreal. Its summary
compares the feet with the ground of the core along the walk (terrain_probe --line).

WalkWatch decides when a walk stops; ue_driver.py feeds it in Unreal, the tests feed it here.
"""

import csv
import math
from dataclasses import dataclass

COLUMNS = ["t", "x", "z", "feet", "mode", "ground", "distance"]
FALLING = "falling"
_EPSILON = 1e-9  # seconds: the sums of the steps of time are not exact


@dataclass(frozen=True)
class Sample:
    t: float
    x: float
    z: float
    feet: float
    mode: str
    ground: float = None  # the trace in Unreal, or None when it hit nothing
    distance: float = 0.0


class WalkWatch:
    """The rules that stop a walk, fed one sample after another."""

    def __init__(self, goal, fall_seconds=0.3, fall_drop=2.0, stuck_seconds=3.0,
                 stuck_meters=0.5, timeout=120.0):
        self.goal = goal
        self.fall_seconds = fall_seconds
        self.fall_drop = fall_drop
        self.stuck_seconds = stuck_seconds
        self.stuck_meters = stuck_meters
        self.timeout = timeout
        self._falling_since = None
        self._last_walking_feet = None
        self._progress = []  # (t, distance) over the last stuck_seconds
        self._previous_feet = None

    def feed(self, s):
        """The reason the walk stops at this sample, or None. A fall is the mode Falling for
        fall_seconds, or the feet fallen by fall_drop since the previous sample whatever the mode
        read (on b7a3f0e a walk down steps of 5 to 8 m saw no Falling)."""
        previous, self._previous_feet = self._previous_feet, s.feet
        if previous is not None and previous - s.feet >= self.fall_drop:
            return "fall"
        if s.mode == FALLING:
            if self._falling_since is None:
                self._falling_since = s.t
            dropped = (self._last_walking_feet is not None and
                       self._last_walking_feet - s.feet >= self.fall_drop)
            if s.t - self._falling_since >= self.fall_seconds - _EPSILON or dropped:
                return "fall"
        else:
            self._falling_since = None
            self._last_walking_feet = s.feet
        if s.distance >= self.goal:
            return "distance"
        if s.t >= self.timeout - _EPSILON:
            return "timeout"
        self._progress.append((s.t, s.distance))
        while self._progress and s.t - self._progress[0][0] > self.stuck_seconds:
            self._progress.pop(0)
        first_t, first_d = self._progress[0]
        if s.t - first_t >= 0.9 * self.stuck_seconds and s.distance - first_d < self.stuck_meters:
            return "stuck"
        return None


def write_walk(path, samples):
    with open(path, "w", newline="", encoding="utf-8") as file:
        out = csv.writer(file)
        out.writerow(COLUMNS)
        for s in samples:
            out.writerow([f"{s.t:.3f}", f"{s.x:.3f}", f"{s.z:.3f}", f"{s.feet:.3f}", s.mode,
                          "" if s.ground is None else f"{s.ground:.3f}", f"{s.distance:.3f}"])


def read_walk(path):
    with open(path, newline="", encoding="utf-8") as file:
        rows = list(csv.DictReader(file))
    return [Sample(float(r["t"]), float(r["x"]), float(r["z"]), float(r["feet"]), r["mode"],
                   float(r["ground"]) if r["ground"] else None, float(r["distance"]))
            for r in rows]


def interpolated(profile, distance):
    """The ground of the core at distance along the walk, linear between the points of the
    profile ((distance, ground) in order), held beyond its ends."""
    if not profile:
        return None
    if distance <= profile[0][0]:
        return profile[0][1]
    for (d0, g0), (d1, g1) in zip(profile, profile[1:]):
        if distance <= d1:
            t = 0.0 if d1 == d0 else (distance - d0) / (d1 - d0)
            return g0 + t * (g1 - g0)
    return profile[-1][1]


def bracket_floor(profile, distance):
    """The lower of the two points of the profile around distance: across a step of the ground
    the line between them would stand above the foot of the step."""
    if not profile:
        return None
    if distance <= profile[0][0]:
        return profile[0][1]
    for (d0, g0), (d1, g1) in zip(profile, profile[1:]):
        if distance <= d1:
            return min(g0, g1)
    return profile[-1][1]


def heading_of(dx, dz):
    """Degrees clockwise from the north (-z) of the direction (dx, dz), in [0, 360)."""
    return math.degrees(math.atan2(dx, -dz)) % 360.0


def _falls(samples, drop):
    """The falls: the mode Falling starting, or the feet fallen by drop between two samples."""
    falls, first, largest = 0, None, 0.0
    for prev, s in zip(samples, samples[1:]):
        fell = prev.feet - s.feet
        largest = max(largest, fell)
        if (s.mode == FALLING and prev.mode != FALLING) or fell >= drop:
            falls += 1
            first = s.distance if first is None else first
    if samples and samples[0].mode == FALLING:
        falls, first = falls + 1, samples[0].distance if first is None else first
    return falls, first, largest


def summarize(samples, reason, profile=None, tolerance=0.5, core=None, heading=None,
              drop=2.0):
    """What a walk did: how far and how long, why it stopped, its falls (the mode Falling, or a
    drop of the feet between two samples), the largest step of the feet on the ground, the least
    clearance over the ground of Unreal, the feet at the start and the end, the heading it
    followed, and the samples whose feet stood below the ground of the core by more than
    tolerance: core, the ground of the core at each sample (terrain_probe at its x, z), or else
    the profile along the heading (the lower point around each sample)."""
    if not samples:
        return {"reason": reason, "distance": 0.0, "seconds": 0.0, "samples": 0, "falls": 0,
                "firstFallAt": None, "largestStep": 0.0, "largestDrop": 0.0,
                "leastClearance": None, "belowCoreGround": 0, "start": None, "end": None}
    falls, first_fall, largest_drop = _falls(samples, drop)
    steps = [abs(s.feet - p.feet) for p, s in zip(samples, samples[1:])
             if s.mode != FALLING and p.mode != FALLING and p.feet - s.feet < drop]
    clearances = [s.feet - s.ground for s in samples if s.ground is not None]
    grounds = core if core is not None and len(core) == len(samples) else [
        bracket_floor(profile, s.distance) if profile else None for s in samples]
    below = sum(1 for s, g in zip(samples, grounds) if g is not None and s.feet < g - tolerance)
    first, last = samples[0], samples[-1]
    followed = heading_of(last.x - first.x, last.z - first.z) if last.distance > 0.5 else None
    deviation = None
    if followed is not None and heading is not None:
        deviation = abs((followed - heading + 180.0) % 360.0 - 180.0)
    return {
        "reason": reason, "distance": last.distance, "seconds": last.t - first.t,
        "samples": len(samples), "falls": falls, "firstFallAt": first_fall,
        "largestStep": max(steps, default=0.0), "largestDrop": largest_drop,
        "leastClearance": min(clearances) if clearances else None, "belowCoreGround": below,
        "start": first.feet, "end": last.feet,
        "coreStart": grounds[0], "coreEnd": grounds[-1],
        "headingFollowed": followed, "deviation": deviation,
    }


def parse_walk(text, default_distance=100.0):
    """A walk of the command line: "place[:heading[:distance]]" (heading in degrees, the
    heading of the place when absent)."""
    parts = text.split(":")
    if not parts[0] or len(parts) > 3:
        raise ValueError(f"a walk is place[:heading[:distance]], not '{text}'")
    heading = float(parts[1]) if len(parts) > 1 and parts[1] else None
    distance = float(parts[2]) if len(parts) > 2 else default_distance
    if distance <= 0.0:
        raise ValueError(f"the distance of a walk must be positive: '{text}'")
    return {"place": parts[0], "heading": heading, "distance": distance}
