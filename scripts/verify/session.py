"""The sessions of Unreal of a verification, short and watched.

The editor is started on the project of a version and its test map with the driver
(ue_driver.py, -ExecutePythonScript), the job given by the environment (TERRAIN_VERIFY_JOB). The
driver writes a heartbeat (JSON: its phase, the place, a counter, done) every second. The watchdog
kills the whole tree of the processes of the editor (taskkill /T /F under Windows) when the
heartbeat stops for longer than the limit of its phase, when the session lasts too long, or when
the editor does not close after the driver said it was done. Nothing here touches the power,
sleep or display settings of the system.
"""

import json
import os
import platform
import signal
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path

JOB_VARIABLE = "TERRAIN_VERIFY_JOB"

# Seconds without a heartbeat, by phase of the driver: the start (the editor, the shaders of a
# first run), the material scripts (their recompilation blocks the editor), the others.
DEFAULT_LIMITS = {"start": 1800.0, "materials": 1200.0, "other": 300.0}


@dataclass
class Limits:
    phases: dict = field(default_factory=lambda: dict(DEFAULT_LIMITS))
    total: float = 3600.0        # seconds of a whole session
    close: float = 120.0         # seconds for the editor to close once the driver is done

    def stale_after(self, phase):
        return self.phases.get(phase or "start", self.phases["other"])


@dataclass
class Outcome:
    exit_code: int = None
    killed: bool = False
    reason: str = ""
    seconds: float = 0.0
    last_beat: dict = field(default_factory=dict)


def editor_command(machine, uproject, driver, offscreen=False, extra=()):
    command = [str(machine.editor_exe()), str(uproject), machine.map,
               f"-ExecutePythonScript={driver}", *machine.editor_args, *extra]
    if offscreen:
        command.append("-RenderOffScreen")
    return command


def kill_tree(process):
    """Kills the process and every process it started."""
    if process.poll() is not None:
        return
    if platform.system() == "Windows":
        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        try:
            os.killpg(os.getpgid(process.pid), signal.SIGKILL)
        except ProcessLookupError:
            pass
    try:
        process.wait(timeout=30)
    except subprocess.TimeoutExpired:
        process.kill()


def read_beat(path):
    try:
        with open(path, encoding="utf-8") as file:
            return json.load(file)
    except (OSError, ValueError):
        return None


def _start(command, env, log):
    options = {}
    if platform.system() == "Windows":
        options["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        options["start_new_session"] = True
    return subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, **options)


def run_watched(command, job_path, heartbeat_path, log_path, limits=None, poll=0.5,
                clock=time.monotonic):
    """Runs the session under the watchdog until the editor exits or is killed."""
    limits = limits or Limits()
    env = dict(os.environ)
    env[JOB_VARIABLE] = str(job_path)
    Path(heartbeat_path).unlink(missing_ok=True)
    start = clock()
    last_change = start
    last_beat = None
    done_at = None
    with open(log_path, "w", encoding="utf-8") as log:
        process = _start(command, env, log)
        while True:
            code = process.poll()
            now = clock()
            beat = read_beat(heartbeat_path)
            if beat is not None and beat != last_beat:
                last_beat, last_change = beat, now
                if beat.get("done") and done_at is None:
                    done_at = now
            if code is not None:
                return Outcome(code, False, "exited", now - start, last_beat or {})
            reason = _kill_reason(limits, now - start, now - last_change, last_beat,
                                  None if done_at is None else now - done_at)
            if reason:
                kill_tree(process)
                return Outcome(process.poll(), True, reason, clock() - start, last_beat or {})
            time.sleep(poll)


def _kill_reason(limits, elapsed, silent, beat, since_done):
    """Why the session must be killed now, or None: once the driver is done, only the time the
    editor takes to close counts."""
    if since_done is not None:
        if since_done > limits.close:
            return f"the editor did not close {limits.close:.0f} s after the driver ended"
        return None
    phase = (beat or {}).get("phase", "start")
    if silent > limits.stale_after(phase):
        return f"no heartbeat for {silent:.0f} s in phase '{phase}'"
    if elapsed > limits.total:
        return f"the session lasted more than {limits.total:.0f} s"
    return None


def sessions(places, per_session):
    """The places split into short sessions of at most per_session each."""
    per_session = max(1, int(per_session))
    return [places[i:i + per_session] for i in range(0, len(places), per_session)]
