"""
Shared preprocessing and feature extraction for the ToF/IMU gait logger.

Import from the numbered scripts:
    from gait_lib import load_session, make_windows
"""
import numpy as np
import pandas as pd

# Must match the CSV header written by the firmware.
COLUMNS = [
    "elapsed_ms", "label",
    "dist1_mm", "status1", "signal1_kcps", "ambient1_kcps", "sigma1_mm",
    "dist2_mm", "status2", "signal2_kcps", "ambient2_kcps", "sigma2_mm",
    "yaw1", "pitch1", "roll1", "yaw2", "pitch2", "roll2",
]

LABELS = ["standing", "level_walk", "stair_up", "stair_down",
          "ramp_up", "ramp_down", "sit_down", "stand_up"]


def load_session(path, sigma_max=None, trim_ms=400, min_seg_ms=1000):
    """
    Load one CSV and clean it up.

    sigma_max defaults to 2.5x the session median, clamped to 15-60 mm.
    Outdoor sessions run much noisier than indoor ones, so a fixed
    threshold either throws away good outdoor data or lets bad indoor
    data through.

    trim_ms drops samples right after a label change - the button press
    never lines up exactly with the actual transition.

    Adds t, d1, d2, ok1, ok2, clean_lab to the frame.
    """
    df = pd.read_csv(path)

    missing = [c for c in COLUMNS if c not in df.columns]
    if missing:
        raise ValueError(f"Missing columns: {missing}\nFound: {list(df.columns)}")

    df["t"] = df["elapsed_ms"] / 1000.0

    if sigma_max is None:
        s_med = pd.concat([df["sigma1_mm"], df["sigma2_mm"]]).median()
        sigma_max = max(15, min(60, s_med * 2.5))
    df["sigma_max_used"] = sigma_max

    # Mark label segments so we can trim the edges and drop short ones.
    df["_seg"] = (df["label"] != df["label"].shift()).cumsum()
    g = df.groupby("_seg")["elapsed_ms"]
    df["_t_in_seg"] = g.transform(lambda x: x - x.min())
    df["_seg_len"] = g.transform(lambda x: x.max() - x.min())
    df["clean_lab"] = (
        (df["_t_in_seg"] >= trim_ms)
        & (df["_seg_len"] >= min_seg_ms)
        & (df["label"] != "none")
    )

    # status 2 means "signal below threshold" - still usable if sigma is sane.
    for i in (1, 2):
        ok = (
            df[f"status{i}"].isin([0, 2])
            & (df[f"sigma{i}_mm"] <= sigma_max)
            & (df[f"dist{i}_mm"] > 50)
            & (df[f"dist{i}_mm"] < 1300)
        )
        df[f"ok{i}"] = ok
        d = df[f"dist{i}_mm"].where(ok)
        df[f"d{i}"] = d.rolling(5, center=True, min_periods=1).median()

    return df


def session_quality(df):
    """Summary stats used by 01_check_quality.py."""
    n = len(df)
    dt = df["elapsed_ms"].diff().dropna()
    hz = 1000 / dt.median() if len(dt) else 0
    dur = (df["elapsed_ms"].max() - df["elapsed_ms"].min()) / 1000
    return {
        "samples": n,
        "duration_s": round(dur, 1),
        "rate_hz": round(hz, 1),
        "tof1_valid_pct": round(100 * df["ok1"].mean(), 1),
        "tof2_valid_pct": round(100 * df["ok2"].mean(), 1),
        "ambient_median": int(df["ambient1_kcps"].median()),
        "sigma_threshold": round(df["sigma_max_used"].iloc[0], 1),
        # A flat zero here usually means the IMU never connected.
        "imu1_active_pct": round(100 * (df["pitch1"].abs() > 0.01).mean(), 1),
        "imu2_active_pct": round(100 * (df["pitch2"].abs() > 0.01).mean(), 1),
    }


def make_windows(paths, keep_labels, win=40, step=10, block_s=8, min_valid=0.4):
    """
    Slice sessions into overlapping windows and pull summary features.

    win=40 / step=10 gives 2 s windows every 0.5 s at 20 Hz.

    blk is an 8-second block id. Pass it as the group to GroupKFold -
    windows overlap, so a random split puts near-identical rows on both
    sides and the score comes out far too high.

    Returns X, y, src, blk, feature_names.
    """
    X, y, src, blk = [], [], [], []

    for path in paths:
        df = load_session(path)

        # Referencing pitch to the standing baseline cancels out
        # differences in how the sensor sits between sessions.
        base1 = df.loc[df["label"] == "standing", "pitch1"].median()
        base2 = df.loc[df["label"] == "standing", "pitch2"].median()
        if np.isnan(base1):
            base1 = df["pitch1"].median()
        if np.isnan(base2):
            base2 = df["pitch2"].median()
        df["dp1"] = df["pitch1"] - base1
        df["dp2"] = df["pitch2"] - base2
        df["dd"] = df["d1"] - df["d2"]

        v = df[df["clean_lab"]].reset_index(drop=True)

        for s in range(0, len(v) - win, step):
            w = v.iloc[s:s + win]
            if w["label"].nunique() != 1:
                continue
            lab = w["label"].iloc[0]
            if lab not in keep_labels:
                continue
            if w["ok1"].mean() < min_valid:
                continue

            feats = []
            for col in ["d1", "d2", "dd", "dp1", "dp2"]:
                a = pd.Series(w[col].values).interpolate().bfill().ffill().values
                if np.isnan(a).all():
                    a = np.zeros(len(w))
                feats += [
                    np.mean(a), np.std(a), np.min(a), np.max(a), np.ptp(a),
                    np.percentile(a, 25), np.percentile(a, 75),
                    np.mean(np.abs(np.diff(a))), np.std(np.diff(a)),
                ]
                # Dominant frequency picks up the step rhythm.
                fft = np.abs(np.fft.rfft(a - a.mean()))
                if len(fft) > 3:
                    feats += [np.argmax(fft[1:]) + 1,
                              fft[1:].max() / (fft[1:].sum() + 1e-9)]
                else:
                    feats += [0, 0]

            X.append(np.nan_to_num(np.array(feats), nan=0, posinf=0, neginf=0))
            y.append(lab)
            src.append(path)
            blk.append(f"{path}_{int(w['t'].iloc[0] // block_s)}")

    names = []
    for col in ["d1", "d2", "dd", "dp1", "dp2"]:
        names += [f"{col}_{k}" for k in
                  ["mean", "std", "min", "max", "ptp", "q25", "q75",
                   "dmean", "dstd", "freq", "freqpow"]]

    return np.array(X), np.array(y), np.array(src), np.array(blk), names


# Column ranges, for comparing what each sensor contributes.
IDX_TOF = list(range(0, 33))    # d1, d2, dd
IDX_IMU = list(range(33, 55))   # dp1, dp2
