#!/usr/bin/env python3
"""
Plot the raw signals so you can see whether activities actually look
different before running any classifier.

Writes to output/:
    fig1_timeseries.png   distance, left-right difference, thigh pitch
    fig2_boxplot.png      per-activity distributions

    python3 02_visualize.py data/LOG_001.csv
"""
import sys
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

import _font  # noqa: F401  - sets the font as a side effect
from gait_lib import load_session

OUT = "output"

COLORS = {
    "none": "#cccccc", "standing": "#4C72B0", "level_walk": "#8A94A6",
    "stair_up": "#3A9E6E", "stair_down": "#E8833A",
    "ramp_up": "#C44E52", "ramp_down": "#DD8452",
    "sit_down": "#CCB974", "stand_up": "#64B5CD",
}


def shade_labels(ax, df):
    """Tint the background by activity label. Returns labels seen."""
    seen = set()
    seg = (df["label"] != df["label"].shift()).cumsum()
    for _, g in df.groupby(seg):
        lab = g["label"].iloc[0]
        ax.axvspan(g["t"].min(), g["t"].max(),
                   color=COLORS.get(lab, "#dddddd"), alpha=0.18, zorder=0)
        seen.add(lab)
    return seen


def plot_timeseries(df):
    fig, axes = plt.subplots(3, 1, figsize=(15, 9), sharex=True)

    v1 = df[df["ok1"]]
    v2 = df[df["ok2"]]
    axes[0].plot(v1["t"], v1["d1"], lw=0.9, color="#2b6cb0", label="left ToF")
    axes[0].plot(v2["t"], v2["d2"], lw=0.9, color="#c53030", label="right ToF",
                 alpha=0.85)
    seen = shade_labels(axes[0], df)
    axes[0].set_ylabel("distance (mm)")
    axes[0].set_title("ToF distance, shaded by activity")
    handles, _ = axes[0].get_legend_handles_labels()
    axes[0].legend(
        handles=handles + [Patch(color=COLORS.get(l, "#ddd"), alpha=0.4, label=l)
                           for l in sorted(seen)],
        fontsize=7, ncol=4)
    axes[0].grid(alpha=0.3)

    both = df[df["ok1"] & df["ok2"]]
    axes[1].plot(both["t"], both["d1"] - both["d2"], lw=0.9, color="#6b46c1")
    axes[1].axhline(0, color="k", lw=0.6, ls="--", alpha=0.5)
    shade_labels(axes[1], df)
    axes[1].set_ylabel("left - right (mm)")
    axes[1].set_title("Left-right distance difference")
    axes[1].grid(alpha=0.3)

    axes[2].plot(df["t"], df["pitch1"], lw=0.8, color="#2b6cb0", label="left")
    axes[2].plot(df["t"], df["pitch2"], lw=0.8, color="#c53030", label="right")
    shade_labels(axes[2], df)
    axes[2].set_ylabel("pitch (deg)")
    axes[2].set_xlabel("time (s)")
    # yaw and roll are unusable at the current mounting angle - see README.
    axes[2].set_title("Thigh pitch (yaw/roll not plotted, see README)")
    axes[2].legend(fontsize=8)
    axes[2].grid(alpha=0.3)

    plt.tight_layout()
    path = os.path.join(OUT, "fig1_timeseries.png")
    plt.savefig(path, dpi=130)
    plt.close()
    print(f"  wrote {path}")


def plot_boxplot(df):
    v = df[df["clean_lab"] & df["ok1"] & df["ok2"]].copy()
    if len(v) < 30:
        print("  skipped boxplot - not enough valid samples")
        return
    v["dd"] = v["d1"] - v["d2"]

    labs = [l for l in v["label"].unique() if (v["label"] == l).sum() > 30]
    if not labs:
        print("  skipped boxplot - no label has enough samples")
        return

    cols = [("d1", "left distance (mm)"),
            ("d2", "right distance (mm)"),
            ("dd", "left - right (mm)"),
            ("pitch1", "left pitch (deg)")]

    fig, axes = plt.subplots(1, 4, figsize=(17, 4.5))
    for ax, (col, title) in zip(axes, cols):
        data = [v[v["label"] == l][col].dropna().values for l in labs]
        bp = ax.boxplot(data, tick_labels=labs, patch_artist=True,
                        showfliers=False)
        for patch, l in zip(bp["boxes"], labs):
            patch.set_facecolor(COLORS.get(l, "#999"))
            patch.set_alpha(0.65)
        ax.set_title(title)
        ax.grid(alpha=0.3, axis="y")
        ax.tick_params(axis="x", rotation=25)

    plt.tight_layout()
    path = os.path.join(OUT, "fig2_boxplot.png")
    plt.savefig(path, dpi=130)
    plt.close()
    print(f"  wrote {path}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    path = sys.argv[1]
    if not os.path.exists(path):
        print(f"not found: {path}")
        sys.exit(1)

    os.makedirs(OUT, exist_ok=True)
    print(f"\n{path}")
    df = load_session(path)
    plot_timeseries(df)
    plot_boxplot(df)
    print(f"\ndone -> {OUT}/\n")


if __name__ == "__main__":
    main()
