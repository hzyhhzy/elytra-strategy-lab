from __future__ import annotations

# Reproduce the direct per-frame L-BFGS-B maximum-climb experiment.
# The best raw result found by this script is archived in
# results/lbfgsb-max-climb-raw.

import argparse
import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path
from time import perf_counter

import numpy as np
from numba import njit, prange
from scipy.optimize import minimize, root


ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "results" / "lbfgsb-max-climb-lbfgsb-run"
RAD = math.pi / 180.0
TICKS_PER_SECOND = 20.0
HORIZONTAL_DRAG = 0.9900000095367432
VERTICAL_DRAG = 0.9800000190734863


@njit(cache=True)
def step_velocity(vx: float, vy: float, pitch_deg: float):
    if pitch_deg < -90.0:
        pitch_deg = -90.0
    elif pitch_deg > 90.0:
        pitch_deg = 90.0

    pitch = pitch_deg * RAD
    cos_pitch = math.cos(pitch)
    sin_pitch = math.sin(pitch)
    old_vx = abs(vx)
    lift = cos_pitch * cos_pitch

    vy += -0.08 + lift * 0.06

    if vy < 0.0 and cos_pitch > 0.0:
        y_accel = vy * -0.1 * lift
        vy += y_accel
        vx += y_accel

    if pitch < 0.0 and cos_pitch > 0.0:
        climb = old_vx * -sin_pitch * 0.04
        vy += climb * 3.2
        vx -= climb

    if cos_pitch > 0.0:
        vx += (old_vx - vx) * 0.1

    vx *= HORIZONTAL_DRAG
    vy *= VERTICAL_DRAG
    return vx, vy


@njit(cache=True)
def period_end_velocity_numba(pitches: np.ndarray, vx: float, vy: float):
    for i in range(pitches.shape[0]):
        vx, vy = step_velocity(vx, vy, pitches[i])
    return vx, vy


@njit(cache=True)
def burnin_start_velocity(pitches: np.ndarray, cycles: int):
    vx = 0.25
    vy = 0.10
    for _ in range(cycles):
        nvx, nvy = period_end_velocity_numba(pitches, vx, vy)
        vx = nvx
        vy = nvy
    return vx, vy


@njit(cache=True)
def eval_cycle_from_burnin(pitches: np.ndarray, cycles: int):
    vx, vy = burnin_start_velocity(pitches, cycles)
    x = 0.0
    y = 0.0
    start_vx = vx
    start_vy = vy
    for i in range(pitches.shape[0]):
        vx, vy = step_velocity(vx, vy, pitches[i])
        x += vx
        y += vy
    closure = max(abs(vx - start_vx), abs(vy - start_vy))
    return x, y, closure


@njit(cache=True)
def roughness_value(pitches: np.ndarray, second_weight: float, tv_weight: float, region_start: int, region_end: int):
    n = pitches.shape[0]
    start = 0 if region_start < 0 else region_start
    end = n if region_end < 0 or region_end > n else region_end
    if end - start < 3:
        return 0.0
    value = 0.0
    if second_weight > 0.0:
        count = 0
        for i in range(start + 1, end - 1):
            d2 = pitches[i - 1] - 2.0 * pitches[i] + pitches[i + 1]
            value += second_weight * d2 * d2
            count += 1
        if count > 0:
            value /= count
    if tv_weight > 0.0:
        count = 0
        tv = 0.0
        for i in range(start, end - 1):
            d = pitches[i + 1] - pitches[i]
            tv += math.sqrt(d * d + 1e-6)
            count += 1
        if count > 0:
            value += tv_weight * tv / count
    return value


@njit(cache=True)
def objective_value(
    pitches: np.ndarray,
    cycles: int,
    second_weight: float,
    tv_weight: float,
    region_start: int,
    region_end: int,
):
    _, y, closure = eval_cycle_from_burnin(pitches, cycles)
    climb_per_tick = y / pitches.shape[0]
    penalty = roughness_value(pitches, second_weight, tv_weight, region_start, region_end)
    return -climb_per_tick + penalty + closure * 100.0


@njit(parallel=True, cache=True)
def fd_gradient(
    pitches: np.ndarray,
    lo: np.ndarray,
    hi: np.ndarray,
    eps: float,
    cycles: int,
    second_weight: float,
    tv_weight: float,
    region_start: int,
    region_end: int,
):
    n = pitches.shape[0]
    grad = np.empty(n, dtype=np.float64)
    for i in prange(n):
        xp = pitches.copy()
        xm = pitches.copy()
        p = pitches[i] + eps
        m = pitches[i] - eps
        if p > hi[i]:
            p = hi[i]
        if m < lo[i]:
            m = lo[i]
        if p == m:
            grad[i] = 0.0
        else:
            xp[i] = p
            xm[i] = m
            fp = objective_value(xp, cycles, second_weight, tv_weight, region_start, region_end)
            fm = objective_value(xm, cycles, second_weight, tv_weight, region_start, region_end)
            grad[i] = (fp - fm) / (p - m)
    return grad


def period_end_velocity_py(pitches: np.ndarray, v: np.ndarray):
    vx = float(v[0])
    vy = float(v[1])
    vx, vy = period_end_velocity_numba(np.asarray(pitches, dtype=np.float64), vx, vy)
    return np.array([vx, vy], dtype=np.float64)


def fixed_point_velocity(pitches: np.ndarray):
    pitches = np.asarray(pitches, dtype=np.float64)
    guess = np.array(burnin_start_velocity(pitches, 400), dtype=np.float64)

    def residual(v):
        return period_end_velocity_py(pitches, v) - v

    sol = root(residual, guess, method="hybr", options={"xtol": 1e-12})
    if sol.success and np.linalg.norm(residual(sol.x), ord=np.inf) < 1e-8:
        return np.asarray(sol.x, dtype=np.float64)
    return guess


@dataclass
class Evaluation:
    period: int
    split: int
    x_gain: float
    y_gain: float
    climb_per_tick: float
    climb_per_second: float
    avg_horizontal_per_second: float
    start_vx: float
    start_vy: float
    end_vx: float
    end_vy: float
    closure: float
    min_y: float
    max_y: float
    min_pitch: float
    max_pitch: float
    rms_delta: float
    max_abs_delta: float
    roughness_l2_second: float


def evaluate_exact(pitches: np.ndarray, split: int):
    pitches = np.asarray(pitches, dtype=np.float64)
    start_v = fixed_point_velocity(pitches)
    vx = float(start_v[0])
    vy = float(start_v[1])
    x = 0.0
    y = 0.0
    ys = []
    trace = []
    for tick, pitch in enumerate(pitches):
        vx, vy = step_velocity(vx, vy, float(pitch))
        x += vx
        y += vy
        ys.append(y)
        trace.append(
            {
                "tick": tick,
                "pitchDeg_internal": float(pitch),
                "angleDeg_pass_to_stepElytra2D": float(-pitch),
                "vx_after": float(vx),
                "vy_after": float(vy),
                "x_after": float(x),
                "relative_y_after": float(y),
            }
        )
    end_v = np.array([vx, vy], dtype=np.float64)
    delta = np.diff(np.r_[pitches, pitches[0]])
    d2 = pitches[:-2] - 2.0 * pitches[1:-1] + pitches[2:]
    period = len(pitches)
    ev = Evaluation(
        period=period,
        split=split,
        x_gain=float(x),
        y_gain=float(y),
        climb_per_tick=float(y / period),
        climb_per_second=float(y / period * TICKS_PER_SECOND),
        avg_horizontal_per_second=float(x / period * TICKS_PER_SECOND),
        start_vx=float(start_v[0]),
        start_vy=float(start_v[1]),
        end_vx=float(end_v[0]),
        end_vy=float(end_v[1]),
        closure=float(np.linalg.norm(end_v - start_v, ord=np.inf)),
        min_y=float(np.min(ys)),
        max_y=float(np.max(ys)),
        min_pitch=float(np.min(pitches)),
        max_pitch=float(np.max(pitches)),
        rms_delta=float(np.sqrt(np.mean(delta * delta))),
        max_abs_delta=float(np.max(np.abs(delta))),
        roughness_l2_second=float(np.mean(d2 * d2)) if len(d2) else 0.0,
    )
    return ev, trace


def bounds_for(n: int, split: int):
    lo = np.empty(n, dtype=np.float64)
    hi = np.empty(n, dtype=np.float64)
    lo[:split] = 0.0
    hi[:split] = 90.0
    lo[split:] = -90.0
    hi[split:] = 0.0
    return lo, hi


def seed_two_segment(n: int, split: int, dive: float, pull: float):
    return np.r_[np.full(split, dive), np.full(n - split, pull)].astype(np.float64)


def seed_bangbang_template(n: int, split: int):
    pitches = np.zeros(n, dtype=np.float64)
    for i in range(split):
        phase = i / max(split - 1, 1)
        if phase < 0.55:
            pitches[i] = 90.0 if (i % 3 == 2) else 0.0
        elif phase < 0.80:
            pitches[i] = 40.0 * (1.0 - (phase - 0.55) / 0.25)
        else:
            pitches[i] = 0.0
    m = n - split
    for j in range(m):
        u = j / max(m - 1, 1)
        pitches[split + j] = -80.0 * (1.0 - u) ** 1.35
    return pitches


def seed_smooth_template(n: int, split: int):
    pitches = np.zeros(n, dtype=np.float64)
    for i in range(split):
        u = i / max(split - 1, 1)
        if u < 0.06:
            pitches[i] = 90.0 * math.sin(0.5 * math.pi * u / 0.06)
        elif u < 0.70:
            z = (u - 0.06) / 0.64
            pitches[i] = 24.0 + 19.0 * z + 2.0 * math.sin(math.pi * z)
        elif u < 0.88:
            z = (u - 0.70) / 0.18
            pitches[i] = 43.0 * (1.0 - z)
        else:
            pitches[i] = 0.0
    m = n - split
    for j in range(m):
        u = j / max(m - 1, 1)
        pitches[split + j] = -82.0 * (1.0 - u) ** 1.45
    return pitches


def seed_late_bangbang_template(n: int, split: int, phase: int):
    pitches = np.zeros(n, dtype=np.float64)
    osc_start = int(round(split * 0.62))
    osc_end = int(round(split * 0.93))
    for i in range(split):
        if i < 2:
            pitches[i] = 0.0
        elif i < 6:
            pitches[i] = 90.0
        elif i < osc_start:
            u = (i - 6) / max(osc_start - 7, 1)
            pitches[i] = 24.0 + 19.0 * (u ** 0.82)
        elif i < osc_end:
            u = (i - osc_start) / max(osc_end - osc_start - 1, 1)
            full = 90.0 if ((i + phase) % 2 == 0) else 0.0
            ramp = min(1.0, max(0.0, u * 5.0))
            pitches[i] = (1.0 - ramp) * 43.0 + ramp * full
        else:
            pitches[i] = 0.0
    m = n - split
    for j in range(m):
        u = j / max(m - 1, 1)
        pitches[split + j] = -90.0 * (1.0 - u) ** 1.55
    return pitches


def optimize_two_segment(n: int, split: int):
    seeds = [
        np.array([30.0, -35.0]),
        np.array([45.0, -45.0]),
        np.array([20.0, -60.0]),
        np.array([60.0, -25.0]),
    ]

    def obj(ab):
        p = seed_two_segment(n, split, float(ab[0]), float(ab[1]))
        return objective_value(p, 160, 0.0, 0.0, -1, -1)

    best = None
    for seed in seeds:
        res = minimize(
            obj,
            seed,
            method="L-BFGS-B",
            bounds=[(0.0, 90.0), (-90.0, 0.0)],
            options={"maxiter": 120, "ftol": 1e-12},
        )
        if best is None or res.fun < best.fun:
            best = res
    return seed_two_segment(n, split, float(best.x[0]), float(best.x[1]))


def optimize_controls(
    seed: np.ndarray,
    split: int,
    *,
    maxiter: int,
    cycles: int,
    eps: float,
    second_weight: float,
    tv_weight: float,
    region_start: int,
    region_end: int,
):
    n = len(seed)
    lo, hi = bounds_for(n, split)
    x0 = np.clip(np.asarray(seed, dtype=np.float64), lo, hi)
    scale = hi - lo
    z0 = (x0 - lo) / scale
    calls = {"n": 0}
    best = {"value": float("inf"), "pitches": x0.copy()}

    def decode(z):
        return np.ascontiguousarray(lo + np.asarray(z, dtype=np.float64) * scale)

    def fun_and_jac(z):
        pitches = decode(z)
        value = objective_value(pitches, cycles, second_weight, tv_weight, region_start, region_end)
        grad_pitch = fd_gradient(pitches, lo, hi, eps, cycles, second_weight, tv_weight, region_start, region_end)
        grad_z = grad_pitch * scale
        calls["n"] += 1
        if float(value) < best["value"]:
            best["value"] = float(value)
            best["pitches"] = pitches.copy()
        return float(value), np.asarray(grad_z, dtype=np.float64)

    start = perf_counter()

    def callback(z):
        if calls["n"] % 5 != 0:
            return
        pitches = decode(z)
        x, y, closure = eval_cycle_from_burnin(pitches, cycles)
        print(
            f"    call={calls['n']:04d} climb={y / n * TICKS_PER_SECOND:.9f} "
            f"dy={y:.9f} closure={closure:.2e} elapsed={perf_counter() - start:.1f}s",
            flush=True,
        )

    res = minimize(
        fun_and_jac,
        z0,
        method="L-BFGS-B",
        jac=True,
        bounds=[(0.0, 1.0)] * n,
        callback=callback,
        options={
            "maxiter": maxiter,
            "maxls": 80,
            "maxcor": 16,
            "ftol": 1e-12,
            "gtol": 1e-7,
        },
    )
    pitches = best["pitches"] if best["pitches"] is not None else decode(res.x)
    return pitches, res


def write_solution(name: str, pitches: np.ndarray, split: int, out_dir: Path):
    ev, trace = evaluate_exact(pitches, split)
    case_dir = out_dir / name
    case_dir.mkdir(parents=True, exist_ok=True)
    with (case_dir / "cycle.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(trace[0].keys()))
        writer.writeheader()
        writer.writerows(trace)
    with (case_dir / "waveform.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["tick", "pitchDeg_internal", "angleDeg_pass_to_stepElytra2D"])
        writer.writeheader()
        for i, pitch in enumerate(pitches):
            writer.writerow(
                {
                    "tick": i,
                    "pitchDeg_internal": f"{float(pitch):.17g}",
                    "angleDeg_pass_to_stepElytra2D": f"{float(-pitch):.17g}",
                }
            )
    with (case_dir / "summary.json").open("w", encoding="utf-8") as f:
        json.dump(ev.__dict__, f, ensure_ascii=False, indent=2)
    return ev, trace, case_dir


def draw_quad(name: str, trace: list[dict], ev: Evaluation, case_dir: Path):
    import matplotlib.pyplot as plt
    from matplotlib import font_manager

    candidates = ["Microsoft YaHei", "SimHei", "SimSun", "Noto Sans CJK SC", "Source Han Sans SC"]
    available = {font.name for font in font_manager.fontManager.ttflist}
    for font in candidates:
        if font in available:
            plt.rcParams["font.sans-serif"] = [font]
            break
    plt.rcParams["axes.unicode_minus"] = False

    ticks = np.array([row["tick"] for row in trace], dtype=float)
    angle = np.array([row["angleDeg_pass_to_stepElytra2D"] for row in trace], dtype=float)
    x = np.array([row["x_after"] for row in trace], dtype=float)
    y = np.array([row["relative_y_after"] for row in trace], dtype=float)
    vx = np.array([row["vx_after"] for row in trace], dtype=float) * TICKS_PER_SECOND
    vy = np.array([row["vy_after"] for row in trace], dtype=float) * TICKS_PER_SECOND

    def lim(values):
        lo = float(np.min(values))
        hi = float(np.max(values))
        if math.isclose(lo, hi):
            return lo - 1.0, hi + 1.0
        pad = (hi - lo) * 0.06
        return lo - pad, hi + pad

    fig, axes = plt.subplots(2, 2, figsize=(14, 9), dpi=160)
    fig.suptitle(f"{name}: 平均升速 {ev.climb_per_second:.6f} 方块/秒，dy {ev.y_gain:+.6f}", fontsize=16)
    axes[0, 0].plot(ticks, angle, color="#007e86", linewidth=1.4)
    axes[0, 0].set_title("仰角时序曲线")
    axes[0, 0].set_xlabel("tick")
    axes[0, 0].set_ylabel("仰角（度）")
    axes[0, 0].set_ylim(-95, 95)
    axes[0, 0].grid(True, alpha=0.25)

    axes[0, 1].plot(ticks, vx, color="#2457a7", linewidth=1.4)
    axes[0, 1].set_title("水平速度曲线")
    axes[0, 1].set_xlabel("tick")
    axes[0, 1].set_ylabel("水平速度（方块/秒）")
    axes[0, 1].set_ylim(*lim(vx))
    axes[0, 1].grid(True, alpha=0.25)

    axes[1, 0].plot(x, y, color="#d56a1d", linewidth=1.7)
    axes[1, 0].set_title("轨迹曲线")
    axes[1, 0].set_xlabel("水平位移（方块）")
    axes[1, 0].set_ylabel("高度变化（方块）")
    axes[1, 0].set_xlim(*lim(x))
    axes[1, 0].set_ylim(*lim(y))
    axes[1, 0].grid(True, alpha=0.25)

    axes[1, 1].plot(ticks, vy, color="#b42318", linewidth=1.4)
    axes[1, 1].axhline(0, color="#10212a", linewidth=0.8, alpha=0.35)
    axes[1, 1].set_title("垂直速度曲线")
    axes[1, 1].set_xlabel("tick")
    axes[1, 1].set_ylabel("垂直速度（方块/秒）")
    axes[1, 1].set_ylim(*lim(vy))
    axes[1, 1].grid(True, alpha=0.25)

    fig.tight_layout(rect=(0, 0, 1, 0.95))
    out = case_dir / "quadrant_zh.png"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--period", type=int, default=255)
    parser.add_argument("--split", type=int, default=165)
    parser.add_argument("--maxiter", type=int, default=120)
    parser.add_argument("--cycles", type=int, default=180)
    parser.add_argument("--eps", type=float, default=0.03)
    parser.add_argument("--smooth-maxiter", type=int, default=160)
    args = parser.parse_args()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    print("warming up numba...", flush=True)
    warm = seed_two_segment(80, 50, 30.0, -30.0)
    lo, hi = bounds_for(80, 50)
    _ = objective_value(warm, 4, 0.0, 0.0, -1, -1)
    _ = fd_gradient(warm, lo, hi, 0.1, 4, 0.0, 0.0, -1, -1)

    n = args.period
    split = args.split
    print(f"period={n} split={split}", flush=True)

    seeds: list[tuple[str, np.ndarray]] = [
        ("two_segment", optimize_two_segment(n, split)),
        ("bangbang_template", seed_bangbang_template(n, split)),
        ("smooth_template", seed_smooth_template(n, split)),
        ("late_bangbang_phase0", seed_late_bangbang_template(n, split, 0)),
        ("late_bangbang_phase1", seed_late_bangbang_template(n, split, 1)),
    ]

    best_ev = None
    best_pitches = None
    summary_rows = []
    for seed_name, seed in seeds:
        print(f"\noptimizing seed={seed_name}", flush=True)
        pitches, res = optimize_controls(
            seed,
            split,
            maxiter=args.maxiter,
            cycles=args.cycles,
            eps=args.eps,
            second_weight=0.0,
            tv_weight=0.0,
            region_start=-1,
            region_end=-1,
        )
        ev, trace, case_dir = write_solution(f"raw_{seed_name}", pitches, split, OUT_DIR)
        draw_quad(f"raw_{seed_name}", trace, ev, case_dir)
        row = {"name": f"raw_{seed_name}", "success": bool(res.success), "message": str(res.message), **ev.__dict__}
        summary_rows.append(row)
        print(
            f"  exact climb={ev.climb_per_second:.9f} dy={ev.y_gain:.9f} "
            f"rmsDelta={ev.rms_delta:.3f} maxDelta={ev.max_abs_delta:.3f}",
            flush=True,
        )
        if best_ev is None or ev.climb_per_second > best_ev.climb_per_second:
            best_ev = ev
            best_pitches = pitches.copy()

    assert best_ev is not None and best_pitches is not None
    ev, trace, case_dir = write_solution("best_raw", best_pitches, split, OUT_DIR)
    draw_quad("best_raw", trace, ev, case_dir)

    print("\nsmoothing high-frequency segment...", flush=True)
    smooth_settings = [
        ("smooth_d2_1e-7", 1e-7, 0.0),
        ("smooth_d2_3e-7", 3e-7, 0.0),
        ("smooth_d2_1e-6", 1e-6, 0.0),
        ("smooth_d2_3e-6", 3e-6, 0.0),
        ("smooth_tv_2e-4", 0.0, 2e-4),
    ]
    region_start = int(round(split * 0.55))
    region_end = int(round(split * 0.92))
    for name, second_weight, tv_weight in smooth_settings:
        print(f"\nregularized {name} region={region_start}:{region_end}", flush=True)
        seed = best_pitches.copy()
        pitches, res = optimize_controls(
            seed,
            split,
            maxiter=args.smooth_maxiter,
            cycles=args.cycles,
            eps=args.eps,
            second_weight=second_weight,
            tv_weight=tv_weight,
            region_start=region_start,
            region_end=region_end,
        )
        ev, trace, case_dir = write_solution(name, pitches, split, OUT_DIR)
        draw_quad(name, trace, ev, case_dir)
        row = {"name": name, "success": bool(res.success), "message": str(res.message), **ev.__dict__}
        summary_rows.append(row)
        print(
            f"  exact climb={ev.climb_per_second:.9f} dy={ev.y_gain:.9f} "
            f"rmsDelta={ev.rms_delta:.3f} maxDelta={ev.max_abs_delta:.3f} d2={ev.roughness_l2_second:.1f}",
            flush=True,
        )

    with (OUT_DIR / "summary.csv").open("w", newline="", encoding="utf-8") as f:
        fieldnames = list(summary_rows[0].keys())
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(summary_rows)
    print(f"\nwrote {OUT_DIR}", flush=True)


if __name__ == "__main__":
    main()
