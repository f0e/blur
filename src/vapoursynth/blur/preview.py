"""Runs blur.py inside another process's vapoursynth (the gui's mpv) rather than through vspipe."""

import sys
import threading
from pathlib import Path

import blur.utils
from vapoursynth import core


class _StderrRouter:
    """vspipe gives each script its own stderr, which blur reads status lines from. scripts run in-process share one,
    so what's written while a script is being evaluated goes to that script's log file instead"""

    def __init__(self, fallback):
        self.fallback = fallback
        self.logs: dict[int, Path] = {}

    def write(self, text: str) -> int:
        log_path = self.logs.get(threading.get_ident())

        if log_path is not None:
            with log_path.open("a", encoding="utf-8") as log_file:
                log_file.write(text)
        elif self.fallback is not None:
            self.fallback.write(text)

        return len(text)

    def flush(self):
        if self.fallback is not None:
            self.fallback.flush()


def _raise_blur_exception(e: blur.utils.BlurException):
    """blur.py exits the process on errors, which would take the host down with it. raising gets the error reported
    by whatever's evaluating the script instead"""
    raise RuntimeError(e.to_json()) from e


def run(script_path: str, script_args: dict[str, str], plugins_path: str, log_path: str):
    # the host's environment might not reach vapoursynth, so plugins it'd normally autoload are loaded here
    if plugins_path:
        core.std.LoadAllPlugins(plugins_path)

    blur.utils.handle_blur_exception = _raise_blur_exception

    if not isinstance(sys.stderr, _StderrRouter):
        sys.stderr = _StderrRouter(sys.stderr)

    router = sys.stderr
    thread = threading.get_ident()
    router.logs[thread] = Path(log_path)

    try:
        script = Path(script_path)

        # vspipe hands script arguments over as globals
        script_globals = {"__name__": "__vapoursynth__", "__file__": str(script), **script_args}
        exec(compile(script.read_text(encoding="utf-8"), str(script), "exec"), script_globals)  # noqa: S102 - running blur.py is the point
    finally:
        del router.logs[thread]
