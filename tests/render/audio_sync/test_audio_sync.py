"""Checks blur's renders keep audio in sync with the video for awkward inputs: edit lists, offset and negative
timestamps, late/early/short/long/gappy audio, variable framerates, and assorted codecs and containers. trimmed
renders are checked too, for sync and for landing on the right frames.

the flashes and tone onsets are found again in the render by their timestamps, the way a player lines them up, and
their offset is compared with the input's.
"""

import functools
import statistics

import pytest
from blur_testing.blur import Config
from blur_testing.inputs import cached_input

from .cases import CASES, Case
from .measure import Measurement, measure

Trim = tuple[float, float | None]

lsmas = Config("lsmas")
bestsource = Config("bestsource", source_plugin="BestSource")
blur_full_first = Config("blur_full_first", blur=True, fully_blur_first_frame=True)

# every case renders with these
CASE_CONFIGS = [
    lsmas,
    bestsource,
    Config("lsmas_gpu", gpu_decoding=True),
    blur_full_first,
    Config("slowmo", timescale=0.5),
    Config("slowmo_pitch", timescale=0.5, pitch=True),
    Config("copy_audio", copy_audio=True),
]

# trimmable cases also render each of these trims with each of these configs
TRIMS: list[Trim] = [(1.0, 6.0), (2.0, None)]
TRIM_CONFIGS = [lsmas, bestsource, blur_full_first, Config("fast", timescale=2.0), Config("quarter", timescale=0.25)]

# awkward trims, only with lsmas: a sliver, an end past the video, and boundaries partway through frames
EXTRA_TRIMS: list[Trim] = [(3.4, 3.7), (5.0, 100.0), (1.009, 6.003)]

# settings that move time around, rendered for the cases marked extra
EXTRA_CONFIGS = [
    Config("interpolate", interpolated_fps=120),
    Config("interpolate_blur", blur=True, output_fps=60, interpolated_fps=240, fully_blur_first_frame=True),
    Config("blur_heavy", blur=True, blur_amount=3, fully_blur_first_frame=True),
    Config("blur_25fps", blur=True, output_fps=25, fully_blur_first_frame=True),
    Config("dedupe", deduplicate=True),
    Config("input_timescale", input_timescale=0.5),
    Config("slowmo_blur_full_first", timescale=0.5, blur=True, fully_blur_first_frame=True),
    Config("mkv_output", container="mkv"),
    Config("mov_output", container="mov"),
    Config("nvenc", gpu_encoding=True),
]


def render_params():
    for case in CASES.values():
        renders = [(c, None) for c in CASE_CONFIGS]
        if case.trim:
            renders += [(c, t) for t in TRIMS for c in TRIM_CONFIGS]
            renders += [(lsmas, t) for t in EXTRA_TRIMS]
        if case.extra:
            renders += [(c, None) for c in EXTRA_CONFIGS]

        for config, trim in renders:
            if config.copy_audio and (trim or not case.copyable):
                continue

            trimmed = f"-trim{trim[0]}-{trim[1] if trim[1] is not None else 'end'}" if trim else ""
            quick = config is lsmas and trim in (None, TRIMS[0])
            yield pytest.param(
                case, config, trim, id=f"{case.name}-{config.name}{trimmed}", marks=() if quick else pytest.mark.full
            )


@pytest.fixture(scope="session")
def source(work):
    @functools.cache
    def get(case: Case) -> Measurement:
        return measure(cached_input(work / "audio_sync" / "inputs" / f"{case.name}.{case.ext}", case.make))

    return get


def trim_window(source: Measurement, trim: Trim | None) -> tuple[float, float]:
    start, end = trim or (0.0, None)
    return max(start, source.first_frame - source.start), end if end is not None else float("inf")


def expected_flashes(source: Measurement, trim: Trim | None, config: Config) -> list[float]:
    start, end = trim_window(source, trim)

    # a flash right at the edge could go either way with rounding, so those are left out
    margin = 0.05
    relative = [f - source.start for f in source.flash_starts]
    return [
        (f - start) / config.speed - config.skipped_time
        for f in relative
        if start + margin <= f < end - margin and (f - start) / config.speed > config.skipped_time + margin
    ]


def check(case: Case, config: Config, source: Measurement, rendered: Measurement, trim: Trim | None):
    """Returns what's wrong with the render, and its measurements."""
    problems = []
    report = []

    # half a frame of tone/flash quantisation is fine when frames are blended or made up. otherwise it should be
    # sample exact, give or take the codecs smearing the tone's onset
    blended = bool(config.blur or config.interpolated_fps or config.deduplicate)
    frame = max(rendered.frame_interval, source.frame_interval) if blended else rendered.frame_interval
    slack = case.slack / config.speed

    sync_tolerance = frame / 2 if blended else 0.004
    if config.timescale and not config.pitch:
        # atempo crossfades an onset in over half its window (sample rate / 24, to a power of two), so where it
        # lands moves around within that
        sync_tolerance = max(0.006, 2048 / 48000 / 4 / config.speed)
    sync_tolerance += slack

    position_tolerance = frame / 2 + 0.002 + slack
    # a trim starts on the nearest frame
    if trim:
        position_tolerance += source.frame_interval / 2 / config.speed

    if len(rendered.onsets) != len(source.onsets):
        problems.append(f"audio tracks: {len(source.onsets)} in, {len(rendered.onsets)} out")

    expected = expected_flashes(source, trim, config)
    flashes = rendered.flash_times(blended)
    relative = [f - rendered.first_frame for f in flashes]
    if any(not any(abs(e - r) <= position_tolerance + 0.03 for r in relative) for e in expected):
        problems.append(
            f"video: expected flashes at {[round(e, 3) for e in expected]}, got {[round(r, 3) for r in relative]}"
        )
    elif expected:
        worst = max((min(relative, key=lambda r: abs(r - e)) - e for e in expected), key=abs)
        if abs(worst) > position_tolerance:
            problems.append(f"video: flashes land {worst * 1000:+.1f}ms off where they should be")

    start, end = trim_window(source, trim)
    for track, (src, out) in enumerate(zip(source.offsets(), rendered.offsets(blended))):
        if not src or not any(start + 0.05 <= o - source.start < end - 0.05 for o in source.onsets[track]):
            continue

        if not out:
            span = rendered.audio_spans[track]
            problems.append(
                f"track {track}: no tones lined up with flashes "
                f"({len(rendered.onsets[track])} tones, audio {span[0]:.3f}s-{span[1]:.3f}s)"
            )
            continue

        reference = statistics.median(src) / config.speed
        error = [o - reference for o in out]
        mean = statistics.mean(error)
        spread = max(error) - min(error)

        audible = [o for o in rendered.onsets[track] if rendered.first_frame - 0.01 <= o <= rendered.last_frame]
        unpaired = max(0, min(len(audible), len(flashes)) - len(out))

        summary = (
            f"track {track}: audio {mean * 1000:+.1f}ms vs source, spread {spread * 1000:.1f}ms, {len(out)} paired"
        )
        report.append(summary)
        if abs(mean) > sync_tolerance or spread > sync_tolerance * 2 + 0.004 or unpaired:
            problems.append(f"{summary}, {unpaired} unpaired")

    return problems, report


@pytest.mark.parametrize("case", CASES.values(), ids=CASES)
def test_input(case, source):
    m = source(case)
    assert m.flashes, "no flashes found"
    for track, (onsets, paired) in enumerate(zip(m.onsets, m.offsets())):
        with_video = [o for o in onsets if m.first_frame <= o <= m.last_frame]
        assert paired and len(paired) >= len(with_video) / 2, (
            f"track {track}: {len(paired)} of {len(with_video)} tones with video lined up with flashes"
        )
    print(f"first frame {m.first_frame:.3f}s, offsets", [f"{statistics.median(o) * 1000:+.1f}ms" for o in m.offsets()])


@pytest.mark.parametrize("case, config, trim", list(render_params()))
def test_render(case, config, trim, source, blur, work, request):
    src = source(case)
    out = work / "audio_sync" / "outputs" / f"{request.node.callspec.id}.{config.container}"
    blur.render(config, src.path, out, *(trim or ()))

    problems, report = check(case, config, src, measure(out), trim)
    print("\n".join(report))
    assert not problems, "\n".join(problems)
