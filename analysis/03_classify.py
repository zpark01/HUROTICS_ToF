#!/usr/bin/env python3
"""
Supervised classification benchmark, plus a breakdown of what each
sensor contributes on its own.

Cross-validation note: windows overlap, so a plain shuffled split leaks
almost-identical rows between train and test. GroupKFold on the time
block is what gives an honest number - the difference is large.

    python3 03_classify.py data/LOG_001.csv data/LOG_002.csv
    python3 03_classify.py data/*.csv
"""
import sys
import os
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import cross_val_score, GroupKFold, StratifiedKFold

from gait_lib import make_windows, IDX_TOF, IDX_IMU

# Only labels present in the data are actually used.
LABEL_SETS = {
    "stairs": ["standing", "level_walk", "stair_up", "stair_down"],
    "ramps": ["standing", "level_walk", "ramp_up", "ramp_down"],
}


def evaluate(name, paths, labels):
    X, y, src, blk, names = make_windows(paths, labels)
    if len(X) < 50:
        print(f"\n[{name}] only {len(X)} windows - skipped")
        return
    uniq, cnt = np.unique(y, return_counts=True)
    if len(uniq) < 2:
        print(f"\n[{name}] single class - skipped")
        return

    baseline = 100 * cnt.max() / cnt.sum()
    n_blocks = len(np.unique(blk))
    cv = GroupKFold(n_splits=min(5, n_blocks))

    print(f"\n{'=' * 60}")
    print(f" {name}   {len(X)} windows, {n_blocks} time blocks")
    print(f"{'=' * 60}")
    print(f"  classes  : {dict(zip(uniq, cnt))}")
    print(f"  majority : {baseline:.1f}%\n")

    for tag, idx in [("ToF only", IDX_TOF),
                     ("IMU only", IDX_IMU),
                     ("ToF + IMU", IDX_TOF + IDX_IMU)]:
        clf = RandomForestClassifier(n_estimators=400, random_state=0)
        try:
            sc = cross_val_score(clf, X[:, idx], y, cv=cv, groups=blk)
            print(f"  {tag:12s} {sc.mean() * 100:5.1f}%   (+/- {sc.std() * 100:.1f})")
        except Exception as e:
            print(f"  {tag:12s} failed: {e}")

    # Shown only to make the leakage gap visible - do not quote this number.
    clf = RandomForestClassifier(n_estimators=400, random_state=0)
    leaky = cross_val_score(clf, X, y,
                            cv=StratifiedKFold(5, shuffle=True, random_state=0))
    print(f"\n  shuffled split (leaky, for reference): {leaky.mean() * 100:.1f}%")

    clf = RandomForestClassifier(n_estimators=400, random_state=0).fit(X, y)
    top = np.argsort(clf.feature_importances_)[::-1][:8]
    print("\n  top features")
    for i in top:
        print(f"    {names[i]:22s} {clf.feature_importances_[i]:.3f}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    paths = [p for p in sys.argv[1:] if os.path.exists(p)]
    if not paths:
        print("no valid files")
        sys.exit(1)

    print(f"\n{len(paths)} file(s)")
    for name, labels in LABEL_SETS.items():
        evaluate(name, paths, labels)
    print()


if __name__ == "__main__":
    main()
