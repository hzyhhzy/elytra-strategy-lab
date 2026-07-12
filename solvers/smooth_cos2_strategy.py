from __future__ import annotations

import argparse
import csv
import json
import math
import shutil
from pathlib import Path

import numpy as np
from scipy.ndimage import gaussian_filter1d

import min_height_span_periodic as phys


TICKS_PER_SECOND = 20.0
INITIAL_VX = 0.0
INITIAL_VY = -3.920003814700875


def read_angles(path: Path) -> list[float]:
    with path.open(newline="", encoding="utf-8") as f:
        return [float(row["angle"]) for row in csv.DictReader(f)]


def write_waveform(path: Path, angles: list[float]) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["tick", "angle"])
        writer.writeheader()
        for i, angle in enumerate(angles, 1):
            writer.writerow({"tick": i, "angle": f"{angle:.17g}"})


def simulate(angles: list[float]) -> tuple[list[dict], dict]:
    vx = INITIAL_VX
    vy = INITIAL_VY
    x = 0.0
    y = 0.0
    best_vx = vx
    best_vy = vy
    best_tick = 0
    rows: list[dict] = []
    for tick, angle in enumerate(angles, 1):
        x, y, vx, vy = phys.step_state(x, y, vx, vy, float(angle))
        if vx > best_vx:
            best_vx = vx
            best_vy = vy
            best_tick = tick
        rows.append(
            {
                "tick": tick,
                "angle": float(angle),
                "x": float(x),
                "y": float(y),
                "vx": float(vx),
                "vy": float(vy),
                "horizontal_bps": float(vx * TICKS_PER_SECOND),
                "vertical_bps": float(vy * TICKS_PER_SECOND),
            }
        )
    deltas = np.diff(np.asarray(angles, dtype=np.float64))
    summary = {
        "best_horizontal_bps": float(best_vx * TICKS_PER_SECOND),
        "best_vx": float(best_vx),
        "best_vy": float(best_vy),
        "best_tick": int(best_tick),
        "ticks_run": len(angles),
        "final_x_m": float(x),
        "final_drop_m": float(y),
        "final_horizontal_bps": float(vx * TICKS_PER_SECOND),
        "final_vertical_bps": float(vy * TICKS_PER_SECOND),
        "min_angle": float(min(angles)) if angles else 0.0,
        "max_angle": float(max(angles)) if angles else 0.0,
        "rms_delta_deg": float(np.sqrt(np.mean(deltas * deltas))) if deltas.size else 0.0,
        "max_abs_delta_deg": float(np.max(np.abs(deltas))) if deltas.size else 0.0,
        "best_cell": -1,
        "visited_cells": 0,
        "angle_step_deg": 0.0,
        "dvx": 0.0,
        "dvy": 0.0,
        "vx_min": 0.0,
        "vx_max": 0.0,
        "vy_min": 0.0,
        "vy_max": 0.0,
    }
    return rows, summary


def write_rows(path: Path, rows: list[dict]) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def write_summary(path: Path, summary: dict) -> None:
    with path.open("w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)


def draw_case(case_dir: Path, title: str) -> Path:
    import matplotlib.pyplot as plt
    from matplotlib import font_manager

    available = {font.name for font in font_manager.fontManager.ttflist}
    for name in ["Microsoft YaHei", "SimHei", "SimSun", "Noto Sans CJK SC", "Source Han Sans SC"]:
        if name in available:
            plt.rcParams["font.sans-serif"] = [name]
            break
    plt.rcParams["axes.unicode_minus"] = False

    with (case_dir / "summary.json").open(encoding="utf-8") as f:
        summary = json.load(f)
    with (case_dir / "trajectory.csv").open(newline="", encoding="utf-8") as f:
        rows = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(f)]

    ticks = np.array([row["tick"] for row in rows])
    angles = np.array([row["angle"] for row in rows])
    x = np.array([row["x"] for row in rows])
    y = np.array([row["y"] for row in rows])
    vx = np.array([row["horizontal_bps"] for row in rows])
    vy = np.array([row["vertical_bps"] for row in rows])

    def padded(values: np.ndarray) -> tuple[float, float]:
        lo = float(np.min(values))
        hi = float(np.max(values))
        if math.isclose(lo, hi):
            return lo - 1.0, hi + 1.0
        pad = (hi - lo) * 0.06
        return lo - pad, hi + pad

    fig, axes = plt.subplots(2, 2, figsize=(14, 9), dpi=180)
    fig.suptitle(
        f"{title}：最大瞬时水平速度 {summary['best_horizontal_bps']:.6f} m/s，"
        f"tick {summary['best_tick']}",
        fontsize=15,
        y=0.98,
    )
    axes[0, 0].plot(ticks, angles, color="#007e86", linewidth=1.2)
    axes[0, 0].set_title("仰角时序")
    axes[0, 0].set_xlabel("tick")
    axes[0, 0].set_ylabel("仰角（度）")
    axes[0, 0].set_ylim(-95, 5)
    axes[0, 0].grid(True, alpha=0.25)

    axes[0, 1].plot(x, y, color="#d56a1d", linewidth=1.35)
    axes[0, 1].set_title("轨迹")
    axes[0, 1].set_xlabel("水平位移（m）")
    axes[0, 1].set_ylabel("高度变化（m）")
    axes[0, 1].set_xlim(*padded(x))
    axes[0, 1].set_ylim(*padded(y))
    axes[0, 1].grid(True, alpha=0.25)

    axes[1, 0].plot(ticks, vx, color="#2457a7", linewidth=1.2)
    axes[1, 0].axvline(summary["best_tick"], color="#b42318", linewidth=0.9, alpha=0.55)
    axes[1, 0].set_title("水平速度")
    axes[1, 0].set_xlabel("tick")
    axes[1, 0].set_ylabel("m/s")
    axes[1, 0].set_ylim(*padded(vx))
    axes[1, 0].grid(True, alpha=0.25)

    axes[1, 1].plot(ticks, vy, color="#b42318", linewidth=1.2)
    axes[1, 1].axhline(0.0, color="#10212a", linewidth=0.8, alpha=0.35)
    axes[1, 1].set_title("垂直速度")
    axes[1, 1].set_xlabel("tick")
    axes[1, 1].set_ylabel("m/s")
    axes[1, 1].set_ylim(*padded(vy))
    axes[1, 1].grid(True, alpha=0.25)

    fig.tight_layout(rect=(0, 0, 1, 0.94))
    out = case_dir / "quadrant_zh.png"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    return out


def cos2_smooth_angles(angles: list[float], sigma: float) -> list[float]:
    rad = np.deg2rad(np.asarray(angles, dtype=np.float64))
    q = np.cos(rad) ** 2
    q_smooth = gaussian_filter1d(q, sigma=sigma, mode="nearest")
    q_smooth = np.clip(q_smooth, 0.0, 1.0)
    # Use negative pitch branch to avoid adding pitch-up behavior that was not
    # present in the relaxed 0/-90 control.
    return (-np.rad2deg(np.arccos(np.sqrt(q_smooth)))).tolist()


def make_case(case_dir: Path, angles: list[float], title: str) -> dict:
    case_dir.mkdir(parents=True, exist_ok=True)
    write_waveform(case_dir / "waveform.csv", angles)
    rows, summary = simulate(angles)
    write_rows(case_dir / "trajectory.csv", rows)
    write_summary(case_dir / "summary.json", summary)
    plot = draw_case(case_dir, title)
    summary["plot"] = str(plot)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-waveform", required=True)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--sigma", type=float, default=8.0)
    parser.add_argument("--sweep", default="2,4,6,8,10,12,16,20")
    args = parser.parse_args()

    source = Path(args.source_waveform)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    angles = read_angles(source)
    osc_summary = make_case(out_dir / "oscillating", angles, "0/-90°震荡解")

    sweep_rows = []
    best_smoothed = None
    for text in [x.strip() for x in args.sweep.split(",") if x.strip()]:
        sigma = float(text)
        smoothed = cos2_smooth_angles(angles, sigma)
        rows, summary = simulate(smoothed)
        sweep_rows.append(
            {
                "sigma": sigma,
                "best_horizontal_bps": summary["best_horizontal_bps"],
                "delta_vs_oscillating_bps": summary["best_horizontal_bps"] - osc_summary["best_horizontal_bps"],
                "best_tick": summary["best_tick"],
                "rms_delta_deg": summary["rms_delta_deg"],
            }
        )
        if best_smoothed is None or abs(sigma - args.sigma) < abs(best_smoothed[0] - args.sigma):
            best_smoothed = (sigma, smoothed)

    with (out_dir / "sigma_sweep.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(sweep_rows[0].keys()))
        writer.writeheader()
        writer.writerows(sweep_rows)

    sigma, smoothed = best_smoothed
    smooth_summary = make_case(out_dir / f"cos2_gaussian_sigma{sigma:g}", smoothed, f"cos²高斯平滑解（sigma={sigma:g} tick）")
    comparison = {
        "source_waveform": str(source),
        "sigma": sigma,
        "oscillating": osc_summary,
        "smoothed": smooth_summary,
        "delta_best_horizontal_bps": smooth_summary["best_horizontal_bps"] - osc_summary["best_horizontal_bps"],
        "relative_delta": (
            smooth_summary["best_horizontal_bps"] - osc_summary["best_horizontal_bps"]
        )
        / osc_summary["best_horizontal_bps"],
    }
    with (out_dir / "comparison.json").open("w", encoding="utf-8") as f:
        json.dump(comparison, f, indent=2, ensure_ascii=False)
    print(json.dumps(comparison, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
