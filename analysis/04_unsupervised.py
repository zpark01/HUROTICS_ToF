#!/usr/bin/env python3
"""
Cluster the windows without showing the classifier any labels, then
check how well the clusters line up with what was actually recorded.

If they line up, the sensors can separate activities on their own and
the button labels are only needed for verification.

    accuracy   clusters mapped to their most common label
    ARI        Adjusted Rand Index; 0 = chance, 1 = identical partition

    python3 04_unsupervised.py data/*.csv
"""
import sys
import os
import numpy as np
import pandas as pd
from sklearn.cluster import KMeans
from sklearn.preprocessing import StandardScaler
from sklearn.decomposition import PCA
from sklearn.metrics import adjusted_rand_score
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import _font  # noqa: F401
from gait_lib import make_windows

OUT = "output"

LABEL_SETS = {
    "stairs": ["standing", "level_walk", "stair_up", "stair_down"],
    "ramps": ["standing", "level_walk", "ramp_up", "ramp_down"],
}

COLORS = {"standing": "#4C72B0", "level_walk": "#8A94A6",
          "stair_up": "#3A9E6E", "stair_down": "#E8833A",
          "ramp_up": "#C44E52", "ramp_down": "#DD8452"}


def run(name, paths, labels):
    X, y, src, blk, _ = make_windows(paths, labels)
    if len(X) < 50:
        print(f"\n[{name}] only {len(X)} windows - skipped")
        return

    Xs = StandardScaler().fit_transform(X)
    k = len(np.unique(y))
    clusters = KMeans(n_clusters=k, n_init=20, random_state=0).fit_predict(Xs)

    ari = adjusted_rand_score(y, clusters)
    table = pd.crosstab(clusters, y)
    mapping = table.idxmax(axis=1).to_dict()
    pred = np.array([mapping[c] for c in clusters])
    acc = (pred == y).mean() * 100

    print(f"\n{'=' * 60}")
    print(f" {name}   {len(X)} windows, {k} clusters")
    print(f"{'=' * 60}")
    print(f"  accuracy : {acc:.1f}%   (chance {100 / k:.0f}%)")
    print(f"  ARI      : {ari:.3f}")
    print("\n  rows = cluster, columns = recorded label")
    print(table.to_string())

    if ari > 0.7:
        print("\n  -> clusters track the labels closely")
    elif ari > 0.4:
        print("\n  -> partial separation, features need work")
    else:
        print("\n  -> weak; this group needs supervised labels")

    pc = PCA(2).fit_transform(Xs)
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.4))
    for lab in np.unique(y):
        m = y == lab
        axes[0].scatter(pc[m, 0], pc[m, 1], s=12, alpha=0.75,
                        color=COLORS.get(lab, "#999"), label=lab,
                        edgecolors="none")
    axes[0].set_title("recorded labels")
    axes[0].legend(fontsize=8, markerscale=1.5)
    for c in range(k):
        m = clusters == c
        axes[1].scatter(pc[m, 0], pc[m, 1], s=12, alpha=0.75,
                        color=COLORS.get(mapping[c], "#999"), edgecolors="none")
    axes[1].set_title(f"clusters, no labels used ({acc:.0f}% match)")
    for ax in axes:
        ax.set_xticks([])
        ax.set_yticks([])

    plt.tight_layout()
    path = os.path.join(OUT, f"fig3_cluster_{name}.png")
    plt.savefig(path, dpi=130)
    plt.close()
    print(f"\n  wrote {path}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    paths = [p for p in sys.argv[1:] if os.path.exists(p)]
    if not paths:
        print("no valid files")
        sys.exit(1)

    os.makedirs(OUT, exist_ok=True)
    print(f"\n{len(paths)} file(s)")
    for name, labels in LABEL_SETS.items():
        run(name, paths, labels)
    print()


if __name__ == "__main__":
    main()
