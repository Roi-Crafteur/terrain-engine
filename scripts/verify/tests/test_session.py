"""The sessions of Unreal (session.py) with stand-in processes: a clean exit, a hung editor killed
with what it started, an editor that does not close, the job passed by the environment."""

import json
import os
import sys
import tempfile
import textwrap
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import fakes  # noqa: E402
import session  # noqa: E402

FAST = session.Limits(phases={"start": 3.0, "materials": 3.0, "other": 1.0}, total=20.0,
                      close=1.0)


def child(folder, body):
    """A stand-in editor: a Python script that writes its heartbeat to argv[1]."""
    script = Path(folder) / "editor.py"
    script.write_text(textwrap.dedent("""
        import json, os, subprocess, sys, time
        beat = sys.argv[1]
        def write(**fields):
            with open(beat + ".tmp", "w") as f:
                json.dump(fields, f)
            os.replace(beat + ".tmp", beat)
        """) + textwrap.dedent(body), encoding="utf-8")
    return script


def running(pid):
    """A process that still runs (a zombie left to an init that never reaps it does not)."""
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    stat = Path(f"/proc/{pid}/stat")
    return not (stat.exists() and stat.read_text().split(")")[-1].split()[0] == "Z")


def watched(folder, body, limits=FAST):
    beat = Path(folder) / "heartbeat.json"
    command = [sys.executable, str(child(folder, body)), str(beat)]
    started = time.monotonic()
    outcome = session.run_watched(command, Path(folder) / "job.json", beat,
                                  Path(folder) / "editor.log", limits, poll=0.1)
    return outcome, time.monotonic() - started


class Watchdog(unittest.TestCase):
    def test_a_clean_session(self):
        with tempfile.TemporaryDirectory() as folder:
            outcome, _ = watched(folder, """
                write(phase="place", n=1, job=os.environ["TERRAIN_VERIFY_JOB"])
                time.sleep(0.3)
                write(phase="done", n=2, done=True, job=os.environ["TERRAIN_VERIFY_JOB"])
                """)
            self.assertEqual((outcome.exit_code, outcome.killed, outcome.reason),
                             (0, False, "exited"))
            self.assertTrue(outcome.last_beat["done"])
            self.assertTrue(outcome.last_beat["job"].endswith("job.json"))

    def test_a_hung_editor_is_killed_with_its_children(self):
        with tempfile.TemporaryDirectory() as folder:
            pid_file = Path(folder) / "grandchild.pid"
            outcome, seconds = watched(folder, f"""
                p = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
                open({str(pid_file)!r}, "w").write(str(p.pid))
                write(phase="place", n=1)
                time.sleep(60)
                """)
            self.assertTrue(outcome.killed)
            self.assertIn("no heartbeat", outcome.reason)
            self.assertLess(seconds, 10.0)
            if os.name != "nt":
                time.sleep(0.2)
                self.assertFalse(running(int(pid_file.read_text())))

    def test_an_editor_that_does_not_close(self):
        with tempfile.TemporaryDirectory() as folder:
            outcome, seconds = watched(folder, """
                write(phase="done", n=1, done=True)
                time.sleep(60)
                """)
            self.assertTrue(outcome.killed)
            self.assertIn("did not close", outcome.reason)
            self.assertLess(seconds, 10.0)

    def test_a_session_too_long(self):
        limits = session.Limits(phases={"start": 30.0, "other": 30.0}, total=1.0, close=30.0)
        with tempfile.TemporaryDirectory() as folder:
            outcome, _ = watched(folder, """
                for n in range(100):
                    write(phase="place", n=n)
                    time.sleep(0.1)
                """, limits)
            self.assertTrue(outcome.killed)
            self.assertIn("lasted more than", outcome.reason)


class Command(unittest.TestCase):
    def test_editor_command(self):
        with tempfile.TemporaryDirectory() as root:
            m = fakes.a_machine(root, editorArgs=["-NoSplash", "-unattended"])
            c = session.editor_command(m, "P/TerrainSandbox.uproject", "D/ue_driver.py")
            self.assertEqual(c[1:4], ["P/TerrainSandbox.uproject", "/Game/TerrainTest/TerrainGasp",
                                      "-ExecutePythonScript=D/ue_driver.py"])
            self.assertIn("-unattended", c)
            self.assertNotIn("-RenderOffScreen", c)
            self.assertIn("-RenderOffScreen", session.editor_command(m, "P", "D", offscreen=True))

    def test_sessions(self):
        self.assertEqual(session.sessions(list(range(7)), 3), [[0, 1, 2], [3, 4, 5], [6]])
        self.assertEqual(session.sessions([1], 0), [[1]])

    def test_read_beat(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "b.json"
            self.assertIsNone(session.read_beat(path))
            path.write_text("{ half", encoding="utf-8")
            self.assertIsNone(session.read_beat(path))
            path.write_text(json.dumps({"phase": "x"}), encoding="utf-8")
            self.assertEqual(session.read_beat(path), {"phase": "x"})


if __name__ == "__main__":
    unittest.main()
