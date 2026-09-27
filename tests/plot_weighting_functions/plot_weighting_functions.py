import sys
from pathlib import Path

import numpy as np
from matplotlib import pyplot as plt

sys.path.insert(1, str((Path(__file__).parent / "../../src/vapoursynth").resolve()))

import blur.weighting


def plot_analysis(frames: int):
    x = list(range(frames))

    # Create large figure with all subplots
    fig, axes = plt.subplots(2, 3, figsize=(18, 10))
    fig.suptitle("Weighting functions", fontsize=18, fontweight="bold")

    # Row 1, Col 1: All basic functions comparison
    functions = {
        "Equal": blur.weighting.equal(frames),
        "Ascending": blur.weighting.ascending(frames),
        "Descending": blur.weighting.descending(frames),
        "Gaussian": blur.weighting.gaussian(frames, standard_deviation=1),
        "Gaussian Reverse": blur.weighting.gaussian_reverse(frames, standard_deviation=1),
        "Gaussian Sym": blur.weighting.gaussian_sym(frames, standard_deviation=1),
        "Pyramid": blur.weighting.pyramid(frames),
        "Vegas": blur.weighting.vegas(frames),
    }
    colors = [
        "#1f77b4",
        "#ff7f0e",
        "#2ca02c",
        "#d62728",
        "#9467bd",
        "#8c564b",
        "#e377c2",
        "#7f7f7f",
    ]

    for i, (name, weights) in enumerate(functions.items()):
        axes[0, 0].plot(x, weights, linewidth=2, color=colors[i], label=name)
    axes[0, 0].set_title("All weighting functions")

    # Row 1, Col 2: Gaussian symmetric with different standard deviations
    std_devs = [0.3, 0.6, 1.0, 1.5, 1.75]
    colors1 = plt.cm.viridis(np.linspace(0, 1, len(std_devs)))
    for i, sd in enumerate(std_devs):
        axes[0, 1].plot(
            x,
            blur.weighting.gaussian_sym(frames, standard_deviation=sd),
            linewidth=2,
            color=colors1[i],
            label=f"σ={sd}",
        )
    axes[0, 1].set_title("Gaussian Symmetric - Different standard deviations")

    # Row 2, Col 2: Gaussian symmetric with different bounds
    bounds = [(0, 1), (0, 2), (0, 3), (-1, 1), (-2, 2)]
    colors3 = plt.cm.cool(np.linspace(0, 1, len(bounds)))
    for i, bound in enumerate(bounds):
        axes[0, 2].plot(
            x,
            blur.weighting.gaussian_sym(frames, bound=bound),
            linewidth=2,
            color=colors3[i],
            label=f"{bound}",
        )
    axes[0, 2].set_title("Gaussian Symmetric - Different bounds")

    # Row 2, Col 1: Gaussian with different standard deviations
    std_devs = [0.3, 0.6, 1.0, 1.5, 1.75]
    colors1 = plt.cm.viridis(np.linspace(0, 1, len(std_devs)))
    for i, sd in enumerate(std_devs):
        axes[1, 0].plot(
            x,
            blur.weighting.gaussian(frames, standard_deviation=sd),
            linewidth=2,
            color=colors1[i],
            label=f"σ={sd}",
        )
    axes[1, 0].set_title("Gaussian - Different standard deviations")

    # Row 1, Col 3: Gaussian with different means
    means = [0.5, 1.0, 1.5, 2.0]
    colors2 = plt.cm.plasma(np.linspace(0, 1, len(means)))
    for i, mean in enumerate(means):
        axes[1, 1].plot(
            x,
            blur.weighting.gaussian(frames, mean=mean, standard_deviation=0.8, bound=(0, 2)),
            linewidth=2,
            color=colors2[i],
            label=f"μ={mean}",
        )
    axes[1, 1].set_title("Gaussian - Different means")

    # Row 2, Col 2: Gaussian with different bounds
    bounds = [(0, 1), (0, 2), (0, 3), (-1, 1), (-2, 2)]
    colors3 = plt.cm.cool(np.linspace(0, 1, len(bounds)))
    for i, bound in enumerate(bounds):
        axes[1, 2].plot(
            x,
            blur.weighting.gaussian(frames, bound=bound),
            linewidth=2,
            color=colors3[i],
            label=f"{bound}",
        )
    axes[1, 2].set_title("Gaussian - Different bounds")

    for ax in axes.flat:
        ax.set_xlabel("Frame")
        ax.set_ylabel("Weight")
        ax.legend(fontsize=9)
        ax.grid(True, alpha=0.3)

    plt.tight_layout()

    return fig


if __name__ == "__main__":
    print("Creating graphs...")

    fig = plot_analysis(frames=300)
    fig.savefig("weighting_functions.pdf", bbox_inches="tight", dpi=300)
