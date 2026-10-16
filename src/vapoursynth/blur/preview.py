"""Runs blur.py inside another process's vapoursynth (the gui's mpv) rather than through vspipe."""

import contextlib
import ctypes
import io
import os
import sys
import threading
import traceback
from pathlib import Path

import blur.utils
import vapoursynth as vs
from vapoursynth import core

# how many times the first frame's cache use the cache can grow to, and the least it's capped to (mb)
CACHE_HEADROOM = 2
MIN_CACHE_CAP = 1024

# open-svpflow can't be unloaded safely: svpflow1's rayon threads outlive the dll and crash the host when they wake,
# and svpflow2 leaks its gpu context. a preview's core unloads them when it's freed, so they're held open for good
_pinned_plugins: dict[str, ctypes.CDLL] = {}


class _StderrRouter:
    """vspipe gives each script its own stderr, which blur reads status lines from. scripts run in-process share one,
    so what's written while a script is being evaluated goes to that script's log file instead"""

    def __init__(self, fallback):
        self.fallback = fallback
        # thread evaluating a script -> its open log
        self.logs: dict[int, int] = {}
        # thread evaluating a script -> the file the gui makes when it gives up on it
        self.cancel_paths: dict[int, str] = {}

    def open(self, log_path: str):
        thread = threading.get_ident()
        self.logs[thread] = os.open(log_path, os.O_WRONLY | os.O_APPEND | os.O_CREAT)
        self.cancel_paths[thread] = log_path + ".cancel"

    def close(self):
        thread = threading.get_ident()
        os.close(self.logs.pop(thread))
        del self.cancel_paths[thread]

    def cancelled(self) -> bool:
        """Whether the gui has given up on the script this thread is evaluating."""
        cancel_path = self.cancel_paths.get(threading.get_ident())
        return cancel_path is not None and os.path.exists(cancel_path)

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


def _cap_cache(output: vs.VideoNode) -> vs.VideoNode:
    """vapoursynth only evicts once its cache reaches max_cache_size, half the ram by default, so a preview's grows
    with every seek though frames from earlier seeks don't make later ones any faster. capping it below what one frame
    needs makes every frame render its inputs several times, so the cap comes from what the first frame used"""
    capped = threading.Event()

    def cap(n: int, f: vs.VideoFrame) -> vs.VideoFrame:
        if not capped.is_set():
            capped.set()
            needed = max(MIN_CACHE_CAP, CACHE_HEADROOM * core.used_cache_size // 2**20)
            core.max_cache_size = min(core.max_cache_size, needed)
        return f

    return core.std.ModifyFrame(output, output, cap)


def _pin_svpflow():
    for namespace in ("svp1", "svp2"):
        plugin = getattr(core, namespace, None)
        if plugin is not None and plugin.plugin_path not in _pinned_plugins:
            _pinned_plugins[plugin.plugin_path] = ctypes.CDLL(plugin.plugin_path)


def run(script_path: str, script_args: dict[str, str], plugins_path: str, log_path: str):
    # the host's environment might not reach vapoursynth, so plugins it'd normally autoload are loaded here
    if plugins_path:
        core.std.LoadAllPlugins(plugins_path)

    _pin_svpflow()

    blur.utils.handle_blur_exception = _raise_blur_exception

    if not isinstance(sys.stderr, _StderrRouter):
        sys.stderr = _StderrRouter(sys.stderr)

    router = sys.stderr
    router.open(log_path)

    # vs-mlrt only fails to import without tensorrt, when there's nothing to build
    with contextlib.suppress(ImportError, RuntimeError):
        from external import vsmlrt

        # an engine build can outlast the preview that started it, and nothing else would stop it
        vsmlrt.builder_cancelled = router.cancelled

    try:
        script = Path(script_path)

        # vspipe hands script arguments over as globals
        script_globals = {"__name__": "__vapoursynth__", "__file__": str(script), **script_args}
        exec(compile(script.read_text(encoding="utf-8"), str(script), "exec"), script_globals)  # noqa: S102 - running blur.py is the point

        output = vs.get_output(0)
        if isinstance(output, vs.VideoOutputTuple):
            _cap_cache(output.clip).set_output(0, alpha=output.alpha, alt_output=output.alt_output)
    except BaseException:
        # the host only reports that the script failed to load, so the reason goes in the log for the gui to find.
        # vspipe prints this itself, which is where renders get it from
        router.write(traceback.format_exc())
        raise
    finally:
        router.close()
