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


def vegas(frames: int) -> list[float]:
    """
    Vegas-style weighting based on frame count.

    If frames is even: [1, 2, 2, ..., 2, 1]
    If frames is odd:  [1, 1, 1, ..., 1]
    """
    if frames % 2 == 0:
        weights = [1] + [2] * (frames - 2) + [1]
    else:
        weights = [1] * frames

    return normalize(weights)


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
):
    match weighting_type:
        case "equal":
            return equal(blur_frames)

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

        case "vegas":
            return vegas(blur_frames)

        case _:
            try:
                weights = [int(x) for x in weighting_type.split(",")]
                return divide(blur_frames, weights)
            except (ValueError, AttributeError):
                raise u.BlurException(
                    f"Invalid blur weighting type: {weighting_type}. Valid options are: 'equal', 'gaussian_sym', 'vegas', 'pyramid', 'gaussian', 'ascending', 'descending', 'gaussian_reverse', or a comma-separated list of custom weights (e.g. '1, 2, 3, 2, 1')."
                )
