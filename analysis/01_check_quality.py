#!/usr/bin/env python3
"""
Sanity-check a freshly recorded session.

Run this the same day you collect, so you can redo a bad run while the
rig is still set up.

    python3 01_check_quality.py data/LOG_001.csv
    python3 01_check_quality.py data/*.csv
"""
import sys
import os
from gait_lib import load_session, session_quality

TARGET_HZ = 50   # what the firmware aims for


def check(path):
    df = load_session(path)
    q = session_quality(df)

    print(f"\n{'=' * 60}")
    print(f" {os.path.basename(path)}")
    print(f"{'=' * 60}")
    print(f"  samples      : {q['samples']}")
    print(f"  duration     : {q['duration_s']} s")
    print(f"  sample rate  : {q['rate_hz']} Hz  (target {TARGET_HZ})")
    print(f"  ToF valid    : left {q['tof1_valid_pct']}%   right {q['tof2_valid_pct']}%")
    print(f"  IMU active   : #1 {q['imu1_active_pct']}%   #2 {q['imu2_active_pct']}%")
    print(f"  ambient      : {q['ambient_median']} kcps (median)")
    print(f"  sigma cutoff : {q['sigma_threshold']} mm (auto)")

    print("\n  Label breakdown")
    counts = df[df["clean_lab"]]["label"].value_counts()
    for lab, n in counts.items():
        print(f"    {lab:12s} {n:5d} samples  ({n / max(q['rate_hz'], 1):.1f} s)")

    print("\n  Checks")
    clean = True

    if q["rate_hz"] < TARGET_HZ * 0.7:
        print(f"    LOW RATE   running at {100 * q['rate_hz'] / TARGET_HZ:.0f}% of target")
        print("               loop is falling behind - check SD flush interval")
        clean = False

    if q["tof1_valid_pct"] < 60 or q["tof2_valid_pct"] < 60:
        print("    LOW YIELD  many ToF samples rejected")
        print("               normal outdoors; indoors check wiring and lens")
        clean = False

    if q["imu1_active_pct"] < 5 or q["imu2_active_pct"] < 5:
        print("    NO IMU     angles are flat zero - that sensor never connected")
        print("               check P0 is pulled to 3.3V and SDA goes to RX")
        clean = False

    if q["ambient_median"] > 2000:
        print("    BRIGHT     high ambient light, expect larger sigma")

    if len(counts) < 2:
        print("    ONE LABEL  only one activity recorded - check the buttons")
        clean = False

    for lab, n in counts.items():
        if n < 100:
            print(f"    SPARSE     '{lab}' has only {n} samples")

    if clean:
        print("    OK         usable for analysis")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    for path in sys.argv[1:]:
        if not os.path.exists(path):
            print(f"not found: {path}")
            continue
        try:
            check(path)
        except Exception as e:
            print(f"{path}: {e}")


if __name__ == "__main__":
    main()
