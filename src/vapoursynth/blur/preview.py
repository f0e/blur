"""Runs blur.py inside another process's vapoursynth (the gui's mpv) rather than through vspipe."""

import io
import os
import sys
import threading
import traceback
from pathlib import Path

import blur.utils
from vapoursynth import core


class _StderrRouter:
    """vspipe gives each script its own stderr, which blur reads status lines from. scripts run in-process share one,
    so what's written while a script is being evaluated goes to that script's log file instead"""

    def __init__(self, fallback):
        self.fallback = fallback
        # thread evaluating a script -> its open log
        self.logs: dict[int, int] = {}

    def open(self, log_path: str):
        self.logs[threading.get_ident()] = os.open(log_path, os.O_WRONLY | os.O_APPEND | os.O_CREAT)

    def close(self):
        os.close(self.logs.pop(threading.get_ident()))

    def write(self, text: str) -> int:
        log = self.logs.get(threading.get_ident())

        if log is not None:
            os.write(log, text.encode("utf-8"))
        elif self.fallback is not None:
            self.fallback.write(text)

        return len(text)

    def flush(self):
        if self.fallback is not None:
            self.fallback.flush()

    def fileno(self) -> int:
        # subprocesses get handed this rather than going through write, e.g. vs-mlrt's trtexec while it builds an
        # engine, so their output lands in the script's log too
        log = self.logs.get(threading.get_ident())
        if log is not None:
            return log

        if self.fallback is None:
            raise io.UnsupportedOperation("no stderr to hand over")

        return self.fallback.fileno()


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
    router.open(log_path)

    try:
        script = Path(script_path)

        # vspipe hands script arguments over as globals
        script_globals = {"__name__": "__vapoursynth__", "__file__": str(script), **script_args}
        exec(compile(script.read_text(encoding="utf-8"), str(script), "exec"), script_globals)  # noqa: S102 - running blur.py is the point
    except BaseException:
        # the host only reports that the script failed to load, so the reason goes in the log for the gui to find.
        # vspipe prints this itself, which is where renders get it from
        router.write(traceback.format_exc())
        raise
    finally:
        router.close()
