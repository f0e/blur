import os
import shutil
from collections.abc import Callable
from pathlib import Path

from filelock import FileLock


def cached_input(path: Path, make: Callable[[Path, Path], None]) -> Path:
    """Makes the input with `make(out, tmp_dir)` unless it's already there. Safe across xdist workers."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with FileLock(path.with_name(path.name + ".lock")):
        if path.exists():
            return path

        tmp = path.with_name(path.name + ".tmp")
        shutil.rmtree(tmp, ignore_errors=True)
        tmp.mkdir()
        try:
            out = tmp / path.name
            make(out, tmp)
            os.replace(out, path)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    return path
