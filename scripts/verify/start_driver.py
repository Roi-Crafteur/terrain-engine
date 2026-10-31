"""Starts the driver of a verification (ue_driver.py) in the Unreal editor.

The launcher writes this file into the throwaway copy of the host project as
Content/Python/init_unreal.py, the startup script the Python plugin of the editor runs at every
start, and never into the project of the user. It does nothing unless the launcher started the
editor (TERRAIN_VERIFY_JOB set): then it imports the driver (TERRAIN_VERIFY_DRIVER) and starts it
on the tick of the editor; the editor stays open until the driver quits it. (The option
-ExecutePythonScript runs a script and then closes the editor: it is never used.)

An init_unreal.py the host project had is kept beside as init_unreal_project.py and run first.
With --start execcmds, the launcher runs this same file by -ExecCmds="py <file>".
"""

import os
import sys
import traceback
from pathlib import Path

MARKER = "Starts the driver of a verification (ue_driver.py) in the Unreal editor."
JOB_VARIABLE = "TERRAIN_VERIFY_JOB"
DRIVER_VARIABLE = "TERRAIN_VERIFY_DRIVER"


def _run_project_init(here):
    project_init = here.with_name("init_unreal_project.py")
    if not project_init.is_file():
        return
    try:
        code = compile(project_init.read_text(encoding="utf-8"), str(project_init), "exec")
        exec(code, {"__name__": "__main__", "__file__": str(project_init)})  # noqa: S102
    except Exception:  # noqa: BLE001 - the startup script of the project is not ours
        traceback.print_exc()


def start(environ=None, here=None):
    """The driver started, or None when the launcher did not start this editor."""
    environ = os.environ if environ is None else environ
    here = Path(__file__) if here is None else Path(here)
    _run_project_init(here)
    if not environ.get(JOB_VARIABLE):
        return None
    driver = environ.get(DRIVER_VARIABLE)
    if not driver or not Path(driver).is_file():
        print(f"[terrain verify] {DRIVER_VARIABLE} does not name the driver: {driver!r}")
        return None
    folder = str(Path(driver).resolve().parent)
    if folder not in sys.path:
        sys.path.insert(0, folder)
    import ue_driver  # noqa: E402 - from the folder of the driver of the run
    return ue_driver.main()


start()
