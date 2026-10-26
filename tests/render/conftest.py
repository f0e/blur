import shutil
from pathlib import Path

import pytest
from blur_testing.blur import Blur
from blur_testing.tools import ROOT


def pytest_addoption(parser):
    parser.addoption("--blur", type=Path, default=ROOT / "bin" / "Debug" / "blur-cli.exe", help="blur-cli to test")
    parser.addoption("--work", type=Path, default=ROOT / ".local" / "render-tests", help="where inputs and renders go")
    parser.addoption("--regenerate", action="store_true", help="remake the cached inputs")
    parser.addoption("--full", action="store_true", help="run the slow tests marked full too")


def pytest_configure(config):
    # only the main process, not each xdist worker
    if config.getoption("regenerate") and not hasattr(config, "workerinput"):
        for inputs in config.getoption("work").glob("*/inputs"):
            shutil.rmtree(inputs)


def pytest_collection_modifyitems(config, items):
    if config.getoption("full"):
        return

    full = [item for item in items if item.get_closest_marker("full")]
    if full:
        config.hook.pytest_deselected(items=full)
        items[:] = [item for item in items if not item.get_closest_marker("full")]


@pytest.fixture(scope="session")
def work(request) -> Path:
    return request.config.getoption("work").resolve()


@pytest.fixture(scope="session")
def blur(request, work, worker_id) -> Blur:
    cli = request.config.getoption("blur").resolve()
    if not cli.exists():
        pytest.fail(f"{cli} doesn't exist, build it or pass --blur")
    return Blur(cli, work / "appdata" / worker_id)
