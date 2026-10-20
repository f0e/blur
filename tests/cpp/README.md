C++ tests for the shared code and the cli, built as `blur-tests` with googletest. A run takes about 15 seconds.

## running

```sh
./bin/Debug/blur-tests.exe                              # everything
./bin/Debug/blur-tests.exe --gtest_filter='CLITest.*'   # just the matching tests
ctest --test-dir out/build/win-debug -j 8               # each test in its own process
```

## layout

Tests for `src/<path>.cpp` go in `tests/cpp/<path>.cpp`, so `src/common/rendering/output.cpp` is tested by `common/rendering/output.cpp`.

`helpers.h` has what the tests share. Use the fixture that gives a test what it touches:

- `TempDirTest`: a fresh folder per test, under a temp folder for the process
- `SettingsTest`: blur's settings (configs, rules, app config) in that folder instead of the user's
- `CLITest`: settings with a fast default config, and `copy_test_clip()` for a short clip made with blur's ffmpeg once per run

Only render where the test needs an output file. Checks that fail before anything renders, and anything that can be tested without a video, should stay that way. Audio sync, trims and formats are covered by rendering in `tests/render`.
