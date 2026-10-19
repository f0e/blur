Tests that render with `blur-cli` and check the output. They're slow, so they aren't part of `blur-tests`.

## running

```sh
cd tests/render
uv run pytest -n 6                        # the quick set, 6 renders at a time
uv run pytest -n 6 --full                 # everything, including the tests marked full
uv run pytest -n 6 -k "negative_cut"      # just the matching tests
uv run pytest --collect-only -q           # list them
```

- `--blur <path>` picks the `blur-cli` to test (`bin/Debug/blur-cli.exe` by default)
- `--work <dir>` is where inputs and renders go (`.local/render-tests` by default). inputs are made once and reused, `--regenerate` remakes them
- add `-rP` to see the measurements for passing tests too

## layout

- `blur_testing/` is shared by every suite: running blur with a config (`blur.py`), making cached inputs (`inputs.py`), and the ffmpeg it uses (`tools.py`)
- each suite is its own folder with a `test_*.py`, like `audio_sync/`

To add a suite, make a folder with a `test_*.py` and an empty `__init__.py`, and use the `blur` and `work` fixtures from `conftest.py`. Mark slow coverage with `pytest.mark.full` so the default run stays quick. Settings that `Config` doesn't have a field for go in its `extra`, by their name in the config file.
