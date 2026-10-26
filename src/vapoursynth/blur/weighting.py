# thanks to: https://github.com/couleur-tweak-tips/smoothie-rs/blob/f1681afe3629bd7b8e3d12a109c89a8e00873248/target/scripts/weighting.py
#            https://github.com/dwiandhikaap/HFR-Resampler/blob/d8f4adddc554161ebd4ebb04ca804eb046036c08/Weights.py

import itertools
import math
import warnings
from collections.abc import Callable, Iterable
from numbers import Number

import blur.utils as u


class InvalidCustomWeighting(Exception):
    def __init__(self, message="Invalid custom weighting function!"):
        self.message = message
        super().__init__(self.message)


def normalize(weights: Iterable[Number]) -> list[float]:
    """Normalize weights to sum to 1."""
    weights = list(weights)
    if min(weights) < 0:
        # Shift negative weights to positive
        weights = [w - min(weights) + 1 for w in weights]
    total = sum(weights)
    return [w / total for w in weights]


def scale_range(n: int, start: Number, end: Number) -> list[float]:
    """Generate n evenly spaced numbers from start to end."""
    if n <= 1:
        return [start] * n
    return [start + i * (end - start) / (n - 1) for i in range(n)]


# Basic weighting functions
def equal(frames: int) -> list[float]:
    """Uniform weights."""
    return [1 / frames] * frames


def cosine(frames: int) -> list[float]:
    """
    Raised cosine (hann) shutter. At blur amount 2 neighbouring output frames' windows sum to a constant, so fast motion
    leaves one continuous trail.
    """
    if frames <= 1:
        return [1.0] * frames

    return normalize([1 - math.cos(2 * math.pi * i / (frames - 1)) for i in range(frames)])


def ascending(frames: int) -> list[float]:
    """Linearly increasing weights."""
    return normalize(range(1, frames + 1))


def descending(frames: int) -> list[float]:
    """Linearly decreasing weights."""
    return normalize(range(frames, 0, -1))


def pyramid(frames: int) -> list[float]:
    """Symmetric pyramid weights (peak at center)."""
    half = (frames - 1) / 2
    weights = [half - abs(i - half) + 1 for i in range(frames)]
    return normalize(weights)


def gaussian(
    frames: int,
    mean: Number = 2,
    standard_deviation: Number = 1,
    bound: tuple[Number, Number] = (0, 2),
) -> list[float]:
    """
    Gaussian bell curve weights.

    Args:
        mean: peak position relative to bound
        standard_deviation: curve width (higher = broader)
        bound: x-axis range [start, end]
    """
    if len(bound) < 2:
        raise ValueError(f"bound must have length 2, got {bound}")
    if len(bound) > 2:
        warnings.warn(f"Using only first 2 values from bound {bound}")

    x_vals = scale_range(frames, bound[0], bound[1])
    weights = [math.exp(-((x - mean) ** 2) / (2 * standard_deviation**2)) for x in x_vals]
    return normalize(weights)


def gaussian_reverse(
    frames: int,
    mean: Number = 2,
    standard_deviation: Number = 1,
    bound: tuple[Number, Number] = (0, 2),
) -> list[float]:
    """
    Reversed Gaussian curve weights (same curve, reversed order).

    Args:
        mean: peak position relative to bound
        standard_deviation: curve width (higher = broader)
        bound: x-axis range [start, end]
    """
    # Get regular gaussian weights then reverse the order
    weights = gaussian(frames, mean, standard_deviation, bound)
    return weights[::-1]


def gaussian_sym(frames: int, standard_deviation: Number = 1, bound: tuple[Number, Number] = (0, 2)) -> list[float]:
    """Symmetric gaussian with peak at center."""
    max_abs = max(abs(b) for b in bound[:2])
    return gaussian(frames, mean=0, standard_deviation=standard_deviation, bound=(-max_abs, max_abs))


def divide(frames: int, weights: list[float]) -> list[float]:
    """
    Stretch weights array to specified frame count.

    Example: frames=10, weights=[1,2] -> [1,1,1,1,1,2,2,2,2,2] (normalized)
    """
    indices = scale_range(frames, 0, len(weights) - 0.1)
    stretched = [weights[int(idx)] for idx in indices]
    return normalize(stretched)


def parse(
    frame_gap: float,
    blur_amount: float,
    weighting_type: str,
    gaussian_std_dev: float,
    gaussian_mean: float,
    gaussian_bound: str,
    sampling: str = "rectangle",
) -> list[float]:
    """
    Weights for the source frames around an output frame, centred on it. `frame_gap` is source frames per output frame.

    rectangle (like sourcedemorender) holds each frame for the frame of time around it, trapezoid (like advancedfx)
    draws straight lines between frames. both keep the shutter exactly `frame_gap * blur_amount` frames long, so the
    frames at the ends get part of a weight. the rest blend a whole, odd number of frames: round up (like smoothie)
    rounds the exposure and adds a frame when it's even, legacy is blur up to 2.45, which truncated the frame gap and
    the exposure before adding one.
    """
    exposure = frame_gap * blur_amount

    if sampling in ("round up", "legacy"):
        frames = int(exposure + 0.5) if sampling == "round up" else int(int(frame_gap) * blur_amount)
        if frames <= 0:
            return []
        if frames % 2 == 0:
            frames += 1
        return get_type_weights(frames, weighting_type, gaussian_std_dev, gaussian_mean, gaussian_bound)

    curve, corners = get_type_curve(weighting_type, gaussian_std_dev, gaussian_mean, gaussian_bound)
    return integrate(exposure, curve, corners, trapezoid=sampling == "trapezoid")


# 5 point gauss-legendre on [0, 1], exact for polynomials up to degree 9
_NODES = [0.5 + 0.5 * x for x in (-0.906179845938664, -0.5384693101056831, 0, 0.5384693101056831, 0.906179845938664)]
_NODE_WEIGHTS = [
    0.5 * w
    for w in (0.2369268850561891, 0.4786286704993665, 0.5688888888888889, 0.4786286704993665, 0.2369268850561891)
]


def integrate(exposure: float, curve: Callable[[float], float], corners: list[float], trapezoid: bool) -> list[float]:
    """
    Each frame's share of `curve` over a shutter `exposure` frames long centred on frame 0: the area under its slice of
    time for rectangle, or under its slice weighted by a hat reaching the neighbouring frames for trapezoid.
    `corners` are where the curve bends, in shutter positions, split on so every piece is smooth.
    """
    half = exposure / 2
    reach = 1 if trapezoid else 0.5
    last = math.ceil(half + reach)

    weights = []
    for i in range(-last, last + 1):
        start, end = max(-half, i - reach), min(half, i + reach)
        if end <= start:
            weights.append(0.0)
            continue

        cuts = {start, end}
        if trapezoid and start < i < end:
            cuts.add(i)
        cuts.update(t for t in (-half + c * exposure for c in corners) if start < t < end)
        cuts = sorted(cuts)

        area = 0.0
        for a, b in itertools.pairwise(cuts):
            for node, node_weight in zip(_NODES, _NODE_WEIGHTS):
                t = a + (b - a) * node
                hat = 1 - abs(t - i) if trapezoid else 1
                area += node_weight * (b - a) * hat * curve((t + half) / exposure)
        weights.append(area)

    while len(weights) > 1 and weights[0] == 0 and weights[-1] == 0:
        weights = weights[1:-1]

    return normalize(weights)


def get_type_curve(
    weighting_type: str,
    gaussian_std_dev: float,
    gaussian_mean: float,
    gaussian_bound: str,
) -> tuple[Callable[[float], float], list[float]]:
    """The weighting as a curve across the shutter from 0 (opening) to 1 (closing), and where it bends."""
    match weighting_type:
        case "equal" | "vegas":
            return lambda _: 1.0, []

        case "cosine":
            return lambda x: 1 - math.cos(2 * math.pi * x), []

        case "ascending":
            return lambda x: x, []

        case "descending":
            return lambda x: 1 - x, []

        case "pyramid":
            return lambda x: 1 - abs(2 * x - 1), [0.5]

        case "gaussian" | "gaussian_reverse" | "gaussian_sym":
            if len(gaussian_bound) < 2:
                raise ValueError(f"bound must have length 2, got {gaussian_bound}")

            start, end = gaussian_bound[0], gaussian_bound[1]
            mean = gaussian_mean
            if weighting_type == "gaussian_sym":
                end = max(abs(start), abs(end))
                start, mean = -end, 0

            def bell(x: float) -> float:
                if weighting_type == "gaussian_reverse":
                    x = 1 - x
                return math.exp(-((start + x * (end - start) - mean) ** 2) / (2 * gaussian_std_dev**2))

            return bell, []

        case _:
            try:
                steps = [int(x) for x in weighting_type.split(",")]
            except (ValueError, AttributeError):
                raise u.BlurException(INVALID_WEIGHTING.format(weighting_type))

            if min(steps) < 0:
                steps = [s - min(steps) + 1 for s in steps]
            corners = [i / len(steps) for i in range(1, len(steps))]
            return lambda x: steps[min(int(x * len(steps)), len(steps) - 1)], corners


INVALID_WEIGHTING = (
    "Invalid blur weighting type: {}. Valid options are: 'equal', 'cosine', 'gaussian_sym', 'pyramid', 'gaussian', "
    "'ascending', 'descending', 'gaussian_reverse', or a comma-separated list of custom weights (e.g. '1, 2, 3, 2, 1')."
)


def get_type_weights(
    blur_frames: int,
    weighting_type: str,
    gaussian_std_dev: float,
    gaussian_mean: float,
    gaussian_bound: str,
) -> list[float]:
    match weighting_type:
        # vegas was equal with the shared ends halved, which every weighting does now
        case "equal" | "vegas":
            return equal(blur_frames)

        case "cosine":
            return cosine(blur_frames)

        case "ascending":
            return ascending(blur_frames)

        case "descending":
            return descending(blur_frames)

        case "pyramid":
            return pyramid(blur_frames)

        case "gaussian":
            return gaussian(
                blur_frames,
                standard_deviation=gaussian_std_dev,
                mean=gaussian_mean,
                bound=gaussian_bound,
            )

        case "gaussian_reverse":
            return gaussian_reverse(
                blur_frames,
                standard_deviation=gaussian_std_dev,
                mean=gaussian_mean,
                bound=gaussian_bound,
            )

        case "gaussian_sym":
            return gaussian_sym(
                blur_frames,
                standard_deviation=gaussian_std_dev,
                bound=gaussian_bound,
            )

        case _:
            try:
                weights = [int(x) for x in weighting_type.split(",")]
                return divide(blur_frames, weights)
            except (ValueError, AttributeError):
                raise u.BlurException(INVALID_WEIGHTING.format(weighting_type))
