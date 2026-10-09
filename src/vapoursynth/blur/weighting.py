# thanks to: https://github.com/couleur-tweak-tips/smoothie-rs/blob/f1681afe3629bd7b8e3d12a109c89a8e00873248/target/scripts/weighting.py
#            https://github.com/dwiandhikaap/HFR-Resampler/blob/d8f4adddc554161ebd4ebb04ca804eb046036c08/Weights.py

import math
import warnings
from collections.abc import Iterable
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


def cosine(frames: int, rectangle: bool = False) -> list[float]:
    """
    Raised cosine (hann) shutter. At blur amount 2 neighbouring output frames' windows sum to a constant, so fast motion
    leaves one continuous trail. With rectangle sampling each frame holds for its slice of time, so it gets the
    window's area over that slice rather than a point sample (which would be zero at both ends).
    """
    if frames <= 1:
        return [1.0] * frames

    if rectangle:

        def area(x: float) -> float:
            return x - math.sin(2 * math.pi * x) / (2 * math.pi)

        return normalize([area((i + 1) / frames) - area(i / frames) for i in range(frames)])

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
    blur_frames: int,
    weighting_type: str,
    gaussian_std_dev: float,
    gaussian_mean: float,
    gaussian_bound: str,
    sampling: str = "rectangle",
):
    # weights must be centred on a frame, so an even count needs one more weight.
    # rectangle (like sourcedemorender and advancedfx's rectangle mode): each frame holds for its slice of time, so the
    # window stays that many frames and the extra weight is zero, putting it half a frame off centre.
    # trapezoid (like advancedfx's default): frames are points the window interpolates between, so it gains a frame
    # and its two ends are shared with the neighbouring output frames, halved so every source frame counts once in
    # total.
    # round up (like smoothie): gains a frame like trapezoid but keeps its ends whole, blending a frame more than asked
    shared_ends = blur_frames % 2 == 0

    if sampling == "rectangle":
        weights = get_type_weights(
            blur_frames, weighting_type, gaussian_std_dev, gaussian_mean, gaussian_bound, rectangle=True
        )
        return weights + [0.0] if shared_ends else weights

    if shared_ends:
        blur_frames += 1

    weights = get_type_weights(blur_frames, weighting_type, gaussian_std_dev, gaussian_mean, gaussian_bound)

    if shared_ends and sampling == "trapezoid":
        weights[0] /= 2
        weights[-1] /= 2
        weights = normalize(weights)

    return weights


def get_type_weights(
    blur_frames: int,
    weighting_type: str,
    gaussian_std_dev: float,
    gaussian_mean: float,
    gaussian_bound: str,
    rectangle: bool = False,
) -> list[float]:
    match weighting_type:
        # vegas was equal with the shared ends halved, which every weighting does now
        case "equal" | "vegas":
            return equal(blur_frames)

        case "cosine":
            return cosine(blur_frames, rectangle)

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
                raise u.BlurException(
                    f"Invalid blur weighting type: {weighting_type}. Valid options are: 'equal', 'cosine', 'gaussian_sym', 'pyramid', 'gaussian', 'ascending', 'descending', 'gaussian_reverse', or a comma-separated list of custom weights (e.g. '1, 2, 3, 2, 1')."
                )
