from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from time import perf_counter

import numpy as np
from numba import njit, prange
from scipy.optimize import minimize


ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "analysis" / "min_height_span_periodic"
TICKS_PER_SECOND = 20.0

GRAVITY = -0.08
LIFT = 0.06
FALL_TO_GLIDE = -0.1
PITCH_UP_X = 0.04
PITCH_UP_Y = 3.2
HORIZONTAL_ALIGN = 0.1
HORIZONTAL_DRAG = 0.9900000095367432
VERTICAL_DRAG = 0.9800000190734863
DEG_TO_RAD = np.float32(math.pi / 180.0)
RAD_TO_INDEX = np.float32(10430.378)
SIN_TABLE = np.sin(np.arange(65536, dtype=np.float64) * (2.0 * math.pi / 65536.0)).astype(np.float32)


@njit(cache=True)
def mth_sin(value: float) -> float:
    v = np.float32(np.float32(value) * RAD_TO_INDEX)
    return float(SIN_TABLE[int(math.trunc(v)) & 65535])


@njit(cache=True)
def mth_cos(value: float) -> float:
    v = np.float32(np.float32(value) * RAD_TO_INDEX + np.float32(16384.0))
    return float(SIN_TABLE[int(math.trunc(v)) & 65535])


@njit(cache=True)
def make_params(angle: float):
    if angle < -90.0:
        angle = -90.0
    elif angle > 90.0:
        angle = 90.0
    mc_pitch_deg = -angle
    pitch_rad = float(np.float32(np.float32(mc_pitch_deg) * DEG_TO_RAD))
    sin_pitch = mth_sin(pitch_rad)
    cos_pitch = mth_cos(pitch_rad)
    horizontal_look = abs(cos_pitch)
    lift = float(np.float32(np.float32(cos_pitch * cos_pitch)))
    look_sign = 1.0
    if cos_pitch < 0.0:
        look_sign = -1.0
    pitch_up = pitch_rad < 0.0
    return sin_pitch, horizontal_look, lift, look_sign, pitch_up


@njit(cache=True)
def step_state(x: float, y: float, vx: float, vy: float, angle: float):
    sin_pitch, horizontal_look, lift, look_sign, pitch_up = make_params(angle)
    old_horizontal_speed = abs(vx)
    next_vx = vx
    next_vy = vy + GRAVITY + lift * LIFT
    if next_vy < 0.0 and horizontal_look > 0.0:
        y_accel = next_vy * FALL_TO_GLIDE * lift
        next_vy += y_accel
        next_vx += look_sign * y_accel
    if pitch_up and horizontal_look > 0.0:
        climb = old_horizontal_speed * -sin_pitch * PITCH_UP_X
        next_vy += climb * PITCH_UP_Y
        next_vx -= look_sign * climb
    if horizontal_look > 0.0:
        next_vx += (look_sign * old_horizontal_speed - next_vx) * HORIZONTAL_ALIGN
    vx = next_vx * HORIZONTAL_DRAG
    vy = next_vy * VERTICAL_DRAG
    x += vx
    y += vy
    return x, y, vx, vy


@njit(cache=True)
def horizon_velocity():
    x = 0.0
    y = 0.0
    vx = 0.0
    vy = -0.1
    for _ in range(3000):
        x, y, vx, vy = step_state(x, y, vx, vy, 0.0)
    return vx, vy


@njit(cache=True)
def run_period_velocity(angles: np.ndarray, vx: float, vy: float):
    x = 0.0
    y = 0.0
    for i in range(angles.shape[0]):
        x, y, vx, vy = step_state(x, y, vx, vy, angles[i])
    return x, y, vx, vy


@njit(cache=True)
def burnin_velocity(angles: np.ndarray, cycles: int, start_vx: float, start_vy: float):
    vx = start_vx
    vy = start_vy
    for _ in range(cycles):
        _, _, vx, vy = run_period_velocity(angles, vx, vy)
    return vx, vy


@njit(cache=True)
def eval_metrics(angles: np.ndarray, cycles: int, start_vx: float, start_vy: float):
    vx, vy = burnin_velocity(angles, cycles, start_vx, start_vy)
    initial_vx = vx
    initial_vy = vy
    x = 0.0
    y = 0.0
    min_y = 0.0
    max_y = 0.0
    for i in range(angles.shape[0]):
        x, y, vx, vy = step_state(x, y, vx, vy, angles[i])
        if y < min_y:
            min_y = y
        if y > max_y:
            max_y = y
    closure = max(abs(vx - initial_vx), abs(vy - initial_vy))
    return x, y, min_y, max_y, max_y - min_y, initial_vx, initial_vy, vx, vy, closure


@njit(cache=True)
def eval_converged(angles: np.ndarray, max_cycles: int, start_vx: float, start_vy: float):
    vx = start_vx
    vy = start_vy
    final_x = 0.0
    final_y = 0.0
    min_y = 0.0
    max_y = 0.0
    closure = 1e30
    cycles_used = max_cycles
    converged = False
    for cycle in range(max_cycles):
        before_vx = vx
        before_vy = vy
        x = 0.0
        y = 0.0
        min_y = 0.0
        max_y = 0.0
        for i in range(angles.shape[0]):
            x, y, vx, vy = step_state(x, y, vx, vy, angles[i])
            if y < min_y:
                min_y = y
            if y > max_y:
                max_y = y
        final_x = x
        final_y = y
        closure = max(abs(vx - before_vx), abs(vy - before_vy))
        if cycle > 8 and closure < 1e-14:
            cycles_used = cycle + 1
            converged = True
            break
    return final_x, final_y, min_y, max_y, max_y - min_y, vx, vy, closure, cycles_used, converged


@njit(cache=True)
def smooth_range_and_dy(angles: np.ndarray, cycles: int, start_vx: float, start_vy: float, temp: float):
    vx, vy = burnin_velocity(angles, cycles, start_vx, start_vy)
    n = angles.shape[0]
    ys = np.empty(n + 1, dtype=np.float64)
    ys[0] = 0.0
    x = 0.0
    y = 0.0
    hard_min = 0.0
    hard_max = 0.0
    for i in range(n):
        x, y, vx, vy = step_state(x, y, vx, vy, angles[i])
        ys[i + 1] = y
        if y < hard_min:
            hard_min = y
        if y > hard_max:
            hard_max = y

    max_ref = ys[0]
    min_ref = ys[0]
    for i in range(1, n + 1):
        if ys[i] > max_ref:
            max_ref = ys[i]
        if ys[i] < min_ref:
            min_ref = ys[i]

    sum_max = 0.0
    sum_min = 0.0
    for i in range(n + 1):
        sum_max += math.exp((ys[i] - max_ref) / temp)
        sum_min += math.exp((min_ref - ys[i]) / temp)
    smooth_max = max_ref + temp * math.log(sum_max)
    smooth_min = min_ref - temp * math.log(sum_min)
    return smooth_max - smooth_min, y, hard_max - hard_min


@njit(cache=True)
def roughness_l2(angles: np.ndarray):
    if angles.shape[0] < 3:
        return 0.0
    total = 0.0
    for i in range(1, angles.shape[0] - 1):
        d2 = angles[i - 1] - 2.0 * angles[i] + angles[i + 1]
        total += d2 * d2
    return total / (angles.shape[0] - 2)


@njit(cache=True)
def objective_value(
    angles: np.ndarray,
    cycles: int,
    start_vx: float,
    start_vy: float,
    temp: float,
    height_weight: float,
    smooth_weight: float,
):
    smooth_span, dy, hard_span = smooth_range_and_dy(angles, cycles, start_vx, start_vy, temp)
    violation = 0.0
    if dy < 0.0:
        violation = -dy
    return smooth_span + height_weight * (violation + violation * violation) + smooth_weight * roughness_l2(angles)


@njit(parallel=True, cache=True)
def fd_gradient(
    angles: np.ndarray,
    lo: np.ndarray,
    hi: np.ndarray,
    eps: float,
    cycles: int,
    start_vx: float,
    start_vy: float,
    temp: float,
    height_weight: float,
    smooth_weight: float,
):
    grad = np.empty(angles.shape[0], dtype=np.float64)
    for i in prange(angles.shape[0]):
        xp = angles.copy()
        xm = angles.copy()
        p = angles[i] + eps
        m = angles[i] - eps
        if p > hi[i]:
            p = hi[i]
        if m < lo[i]:
            m = lo[i]
        if p == m:
            grad[i] = 0.0
        else:
            xp[i] = p
            xm[i] = m
            fp = objective_value(xp, cycles, start_vx, start_vy, temp, height_weight, smooth_weight)
            fm = objective_value(xm, cycles, start_vx, start_vy, temp, height_weight, smooth_weight)
            grad[i] = (fp - fm) / (p - m)
    return grad


def parse_periods(text: str) -> list[int]:
    if ":" in text:
        a, b, c = [int(x) for x in text.split(":")]
        return list(range(a, b + 1, c))
    return [int(x) for x in text.split(",") if x.strip()]


def sign_bounds(period: int, split: int):
    lo = np.empty(period, dtype=np.float64)
    hi = np.empty(period, dtype=np.float64)
    lo[:split] = -90.0
    hi[:split] = 0.0
    lo[split:] = 0.0
    hi[split:] = 90.0
    return lo, hi


def seed_const(period: int, split: int, a: float, b: float) -> np.ndarray:
    return np.r_[np.full(split, a), np.full(period - split, b)].astype(np.float64)


def seed_ramp(period: int, split: int, kind: str, rng: np.random.Generator | None = None) -> np.ndarray:
    arr = np.zeros(period, dtype=np.float64)
    if kind == "shallow":
        arr[:split] = np.linspace(-5.0, -28.0, split)
        arr[split:] = np.linspace(48.0, 2.0, period - split)
    elif kind == "deep":
        arr[:split] = np.linspace(-75.0, -8.0, split)
        arr[split:] = np.linspace(70.0, 0.0, period - split)
    elif kind == "valley":
        for i in range(split):
            u = i / max(1, split - 1)
            arr[i] = -8.0 - 35.0 * math.sin(math.pi * u) ** 2
        for i in range(split, period):
            u = (i - split) / max(1, period - split - 1)
            arr[i] = 62.0 * (1.0 - u) ** 1.2
    elif kind == "random" and rng is not None:
        neg_cp = rng.uniform(-70.0, -2.0, size=8)
        pos_cp = rng.uniform(1.0, 80.0, size=8)
        neg_x = np.linspace(0.0, 1.0, len(neg_cp))
        pos_x = np.linspace(0.0, 1.0, len(pos_cp))
        if split > 0:
            arr[:split] = np.interp(np.linspace(0.0, 1.0, split), neg_x, neg_cp)
        if period - split > 0:
            arr[split:] = np.interp(np.linspace(0.0, 1.0, period - split), pos_x, pos_cp)
    else:
        arr[:split] = -25.0
        arr[split:] = 35.0
    return arr


@dataclass
class Candidate:
    period: int
    split: int
    name: str
    score: float
    seed: np.ndarray


@dataclass
class Summary:
    name: str
    period: int
    split: int
    height_span: float
    dy: float
    dx: float
    avg_horizontal_bps: float
    min_y: float
    max_y: float
    start_vx: float
    start_vy: float
    end_vx: float
    end_vy: float
    closure: float
    cycles_used: int
    converged: bool
    min_angle: float
    max_angle: float
    rms_delta_deg: float
    max_abs_delta_deg: float
    success: bool
    message: str


def coarse_candidates(args, start_vx: float, start_vy: float) -> list[Candidate]:
    rng = np.random.default_rng(args.seed)
    candidates: list[Candidate] = []
    periods = parse_periods(args.periods)
    split_ratios = [float(x) for x in args.split_ratios.split(",") if x.strip()]
    const_neg = [-90.0, -75.0, -60.0, -45.0, -30.0, -15.0, -5.0]
    const_pos = [5.0, 15.0, 30.0, 45.0, 60.0, 75.0, 90.0]

    for period in periods:
        for ratio in split_ratios:
            split = max(1, min(period - 1, int(round(period * ratio))))
            seeds: list[tuple[str, np.ndarray]] = []
            for a in const_neg:
                for b in const_pos:
                    seeds.append((f"const_{a:g}_{b:g}", seed_const(period, split, a, b)))
            for kind in ["shallow", "deep", "valley"]:
                seeds.append((kind, seed_ramp(period, split, kind)))
            for i in range(args.random_starts):
                seeds.append((f"random{i}", seed_ramp(period, split, "random", rng)))

            for name, seed in seeds:
                score = objective_value(
                    seed,
                    args.coarse_cycles,
                    start_vx,
                    start_vy,
                    args.temp,
                    args.height_weight,
                    args.smooth_weight,
                )
                candidates.append(Candidate(period, split, name, float(score), seed))

    candidates.sort(key=lambda c: c.score)
    return candidates[: args.top]


def optimize_candidate(candidate: Candidate, args, start_vx: float, start_vy: float, out_dir: Path):
    lo, hi = sign_bounds(candidate.period, candidate.split)
    scale = hi - lo
    x0 = np.clip(candidate.seed, lo, hi)
    z0 = (x0 - lo) / scale
    calls = {"n": 0}
    best = {"value": float("inf"), "z": z0.copy()}
    started = perf_counter()

    def decode(z: np.ndarray) -> np.ndarray:
        return np.ascontiguousarray(lo + np.asarray(z, dtype=np.float64) * scale, dtype=np.float64)

    def fun_jac(z: np.ndarray):
        angles = decode(z)
        value = objective_value(
            angles,
            args.warmup_cycles,
            start_vx,
            start_vy,
            args.temp,
            args.height_weight,
            args.smooth_weight,
        )
        grad_angles = fd_gradient(
            angles,
            lo,
            hi,
            args.eps,
            args.warmup_cycles,
            start_vx,
            start_vy,
            args.temp,
            args.height_weight,
            args.smooth_weight,
        )
        calls["n"] += 1
        if float(value) < best["value"]:
            best["value"] = float(value)
            best["z"] = np.asarray(z, dtype=np.float64).copy()
        return float(value), np.asarray(grad_angles * scale, dtype=np.float64)

    def callback(z: np.ndarray):
        if calls["n"] % args.print_every != 0:
            return
        angles = decode(z)
        dx, dy, min_y, max_y, span, *_ = eval_metrics(angles, args.validate_cycles, start_vx, start_vy)
        print(
            f"  call={calls['n']:04d} T={candidate.period} split={candidate.split} "
            f"span={span:.6f} dy={dy:+.6f} elapsed={perf_counter() - started:.1f}s",
            flush=True,
        )

    res = minimize(
        fun_jac,
        z0,
        method="L-BFGS-B",
        jac=True,
        bounds=[(0.0, 1.0)] * candidate.period,
        callback=callback,
        options={
            "maxiter": args.maxiter,
            "maxls": args.maxls,
            "maxcor": args.maxcor,
            "ftol": args.ftol,
            "gtol": args.gtol,
            "maxfun": args.maxfun,
        },
    )
    angles = decode(best["z"])
    summary, rows = evaluate_solution(
        f"T{candidate.period}_S{candidate.split}_{candidate.name}",
        angles,
        candidate.split,
        start_vx,
        start_vy,
        args.validate_cycles,
        bool(res.success),
        str(res.message),
    )
    case_dir = out_dir / summary.name.replace(":", "_")
    write_solution(case_dir, angles, rows, summary)
    return summary, angles, rows, case_dir


def evaluate_solution(
    name: str,
    angles: np.ndarray,
    split: int,
    start_vx: float,
    start_vy: float,
    validate_cycles: int,
    success: bool,
    message: str,
):
    dx, dy, min_y, max_y, span, vx, vy, closure, cycles_used, converged = eval_converged(
        angles, validate_cycles, start_vx, start_vy
    )
    # Reconstruct the displayed cycle from the converged end velocity. Since the
    # velocity fixed point is closed, using the final velocity as the start gives
    # the same cycle within numerical noise.
    x = 0.0
    y = 0.0
    rows = []
    initial_vx = vx
    initial_vy = vy
    min_trace_y = 0.0
    max_trace_y = 0.0
    for i, angle in enumerate(angles, 1):
        x, y, vx, vy = step_state(x, y, vx, vy, float(angle))
        if y < min_trace_y:
            min_trace_y = y
        if y > max_trace_y:
            max_trace_y = y
        rows.append(
            {
                "tick": i,
                "angle": float(angle),
                "x": float(x),
                "y": float(y),
                "vx": float(vx),
                "vy": float(vy),
            }
        )
    delta = np.diff(angles)
    summary = Summary(
        name=name,
        period=len(angles),
        split=split,
        height_span=float(max_trace_y - min_trace_y),
        dy=float(y),
        dx=float(x),
        avg_horizontal_bps=float(x / (len(angles) / TICKS_PER_SECOND)),
        min_y=float(min_trace_y),
        max_y=float(max_trace_y),
        start_vx=float(initial_vx),
        start_vy=float(initial_vy),
        end_vx=float(vx),
        end_vy=float(vy),
        closure=float(max(abs(vx - initial_vx), abs(vy - initial_vy))),
        cycles_used=int(cycles_used),
        converged=bool(converged),
        min_angle=float(np.min(angles)),
        max_angle=float(np.max(angles)),
        rms_delta_deg=float(np.sqrt(np.mean(delta * delta))) if len(delta) else 0.0,
        max_abs_delta_deg=float(np.max(np.abs(delta))) if len(delta) else 0.0,
        success=success,
        message=message,
    )
    return summary, rows


def write_csv(path: Path, rows: list[dict]) -> None:
    if not rows:
        return
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def write_solution(case_dir: Path, angles: np.ndarray, rows: list[dict], summary: Summary) -> None:
    case_dir.mkdir(parents=True, exist_ok=True)
    write_csv(
        case_dir / "waveform.csv",
        [{"tick": i, "angle": f"{float(angle):.17g}"} for i, angle in enumerate(angles)],
    )
    write_csv(case_dir / "trajectory.csv", rows)
    with (case_dir / "summary.json").open("w", encoding="utf-8") as f:
        json.dump(asdict(summary), f, ensure_ascii=False, indent=2)


def draw_solution(case_dir: Path, title: str) -> Path:
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
    with (case_dir / "waveform.csv").open(newline="", encoding="utf-8") as f:
        waveform = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(f)]
    with (case_dir / "trajectory.csv").open(newline="", encoding="utf-8") as f:
        rows = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(f)]

    angle_ticks = np.array([row["tick"] for row in waveform], dtype=float)
    angles = np.array([row["angle"] for row in waveform], dtype=float)
    ticks = np.array([row["tick"] for row in rows], dtype=float)
    x = np.array([row["x"] for row in rows], dtype=float)
    y = np.array([row["y"] for row in rows], dtype=float)
    vx = np.array([row["vx"] for row in rows], dtype=float) * TICKS_PER_SECOND
    vy = np.array([row["vy"] for row in rows], dtype=float) * TICKS_PER_SECOND

    def padded(values: np.ndarray) -> tuple[float, float]:
        lo = float(np.min(values))
        hi = float(np.max(values))
        if math.isclose(lo, hi):
            return lo - 1.0, hi + 1.0
        pad = (hi - lo) * 0.06
        return lo - pad, hi + pad

    fig, axes = plt.subplots(2, 2, figsize=(14, 9), dpi=180)
    fig.suptitle(
        f"{title}：高度振幅 {summary['height_span']:.6f} m，dy {summary['dy']:+.6f} m/cycle，"
        f"周期 {summary['period']} tick",
        fontsize=15,
        y=0.98,
    )

    axes[0, 0].plot(angle_ticks, angles, color="#007e86", linewidth=1.35)
    axes[0, 0].axvline(summary["split"], color="#10212a", linewidth=0.9, alpha=0.35)
    axes[0, 0].set_title("仰角时序曲线")
    axes[0, 0].set_xlabel("tick")
    axes[0, 0].set_ylabel("仰角（度）")
    axes[0, 0].set_ylim(-95, 95)
    axes[0, 0].grid(True, alpha=0.25)

    axes[0, 1].plot(x, y, color="#d56a1d", linewidth=1.65)
    axes[0, 1].axhline(0.0, color="#10212a", linewidth=0.8, alpha=0.35)
    axes[0, 1].set_title("轨迹曲线")
    axes[0, 1].set_xlabel("水平位移（方块）")
    axes[0, 1].set_ylabel("高度变化（方块）")
    axes[0, 1].set_xlim(*padded(x))
    axes[0, 1].set_ylim(*padded(y))
    axes[0, 1].grid(True, alpha=0.25)

    axes[1, 0].plot(ticks, vx, color="#2457a7", linewidth=1.35)
    axes[1, 0].set_title("水平速度曲线")
    axes[1, 0].set_xlabel("tick")
    axes[1, 0].set_ylabel("方块/秒")
    axes[1, 0].set_ylim(*padded(vx))
    axes[1, 0].grid(True, alpha=0.25)

    axes[1, 1].plot(ticks, vy, color="#b42318", linewidth=1.35)
    axes[1, 1].axhline(0.0, color="#10212a", linewidth=0.8, alpha=0.35)
    axes[1, 1].set_title("垂直速度曲线")
    axes[1, 1].set_xlabel("tick")
    axes[1, 1].set_ylabel("方块/秒")
    axes[1, 1].set_ylim(*padded(vy))
    axes[1, 1].grid(True, alpha=0.25)

    fig.tight_layout(rect=(0, 0, 1, 0.94))
    out = case_dir / "quadrant_zh.png"
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    return out


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--periods", default="100:260:20")
    parser.add_argument("--split-ratios", default="0.45,0.55,0.65,0.75")
    parser.add_argument("--top", type=int, default=8)
    parser.add_argument("--random-starts", type=int, default=1)
    parser.add_argument("--coarse-cycles", type=int, default=120)
    parser.add_argument("--warmup-cycles", type=int, default=100)
    parser.add_argument("--validate-cycles", type=int, default=700)
    parser.add_argument("--maxiter", type=int, default=60)
    parser.add_argument("--maxfun", type=int, default=200000)
    parser.add_argument("--maxls", type=int, default=30)
    parser.add_argument("--maxcor", type=int, default=12)
    parser.add_argument("--eps", type=float, default=0.05)
    parser.add_argument("--temp", type=float, default=0.20)
    parser.add_argument("--height-weight", type=float, default=500.0)
    parser.add_argument("--smooth-weight", type=float, default=0.0)
    parser.add_argument("--ftol", type=float, default=1e-12)
    parser.add_argument("--gtol", type=float, default=1e-6)
    parser.add_argument("--seed", type=int, default=20260709)
    parser.add_argument("--print-every", type=int, default=10)
    args = parser.parse_args()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    run_dir = OUT_DIR / "runs"
    run_dir.mkdir(parents=True, exist_ok=True)

    print("warming up numba...", flush=True)
    start_vx, start_vy = horizon_velocity()
    warm = seed_const(100, 55, -45.0, 45.0)
    lo, hi = sign_bounds(100, 55)
    _ = objective_value(warm, 2, start_vx, start_vy, args.temp, args.height_weight, args.smooth_weight)
    _ = fd_gradient(warm, lo, hi, 0.1, 2, start_vx, start_vy, args.temp, args.height_weight, args.smooth_weight)

    print("coarse screening...", flush=True)
    candidates = coarse_candidates(args, start_vx, start_vy)
    write_csv(
        OUT_DIR / "coarse_selected.csv",
        [
            {"rank": i + 1, "period": c.period, "split": c.split, "seed": c.name, "score": c.score}
            for i, c in enumerate(candidates)
        ],
    )
    for i, c in enumerate(candidates, 1):
        print(f"  #{i}: T={c.period} split={c.split} seed={c.name} score={c.score:.6f}", flush=True)

    summaries: list[Summary] = []
    best_summary: Summary | None = None
    best_angles: np.ndarray | None = None
    best_rows: list[dict] | None = None
    best_dir: Path | None = None

    for i, c in enumerate(candidates, 1):
        print(f"\n[{i}/{len(candidates)}] optimizing T={c.period} split={c.split} seed={c.name}", flush=True)
        summary, angles, rows, case_dir = optimize_candidate(c, args, start_vx, start_vy, run_dir)
        summaries.append(summary)
        feasible = summary.dy >= -1e-7 and summary.converged
        print(
            f"  result span={summary.height_span:.9f} dy={summary.dy:+.9f} "
            f"avgH={summary.avg_horizontal_bps:.6f} feasible={int(feasible)}",
            flush=True,
        )
        if feasible and (best_summary is None or summary.height_span < best_summary.height_span):
            best_summary = summary
            best_angles = angles.copy()
            best_rows = rows
            best_dir = case_dir

    if summaries:
        write_csv(OUT_DIR / "summary.csv", [asdict(s) for s in summaries])

    if best_summary is None:
        print("\nNo feasible converged result found.", flush=True)
        return

    best_case_dir = OUT_DIR / "best"
    write_solution(best_case_dir, best_angles, best_rows, best_summary)
    plot = draw_solution(best_case_dir, "周期稳态不掉高的最小高度振幅")
    print("\nBEST")
    print(json.dumps(asdict(best_summary), ensure_ascii=False, indent=2), flush=True)
    print(f"best run dir: {best_dir}", flush=True)
    print(f"best plot: {plot}", flush=True)


if __name__ == "__main__":
    main()
