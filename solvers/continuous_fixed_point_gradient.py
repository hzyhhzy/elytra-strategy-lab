from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import asdict
from pathlib import Path
from time import perf_counter

import numpy as np
from numba import njit
from scipy.optimize import minimize

import min_height_span_periodic as mc


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOURCE = (
    ROOT
    / "analysis"
    / "coordinate_min_height_span_below2557_best_refine2"
    / "best"
    / "waveform.csv"
)
DEFAULT_OUT = ROOT / "analysis" / "constrained_height_span_slsqp"
RAD_PER_DEG = math.pi / 180.0


def read_angles(path: Path) -> np.ndarray:
    with path.open(newline="", encoding="utf-8") as f:
        return np.array([float(row["angle"]) for row in csv.DictReader(f)], dtype=np.float64)


@njit(cache=True)
def continuous_step_with_jac(vx: float, vy: float, angle: float):
    """Continuous-trig counterpart of Minecraft's one-tick velocity update.

    Returns the new velocity, its 2x2 Jacobian with respect to the old velocity,
    and its partial derivative with respect to the angle in degrees.
    """
    theta = angle * RAD_PER_DEG
    sin_a = math.sin(theta)
    cos_a = math.cos(theta)
    lift = cos_a * cos_a
    d_lift = -2.0 * sin_a * cos_a * RAD_PER_DEG
    gravity_lift = mc.GRAVITY + mc.LIFT * lift
    d_gravity_lift = mc.LIFT * d_lift
    raw_vy = vy + gravity_lift

    if raw_vy < 0.0 and cos_a > 0.0:
        glide = mc.FALL_TO_GLIDE * lift
        d_glide = mc.FALL_TO_GLIDE * d_lift
        vx0 = vx + glide * raw_vy
        vy0 = raw_vy * (1.0 + glide)
        j00 = 1.0
        j01 = glide
        j10 = 0.0
        j11 = 1.0 + glide
        da0 = d_glide * raw_vy + glide * d_gravity_lift
        da1 = d_gravity_lift * (1.0 + glide) + raw_vy * d_glide
    else:
        vx0 = vx
        vy0 = raw_vy
        j00 = 1.0
        j01 = 0.0
        j10 = 0.0
        j11 = 1.0
        da0 = 0.0
        da1 = d_gravity_lift

    # Use the right derivative at the continuous pitch-up kink. The value at
    # exactly zero is unchanged, while SLSQP can see directions that enter the
    # positive-angle branch instead of treating a zero plateau as stationary.
    if angle >= 0.0:
        transfer = mc.PITCH_UP_X * sin_a
        d_transfer = mc.PITCH_UP_X * cos_a * RAD_PER_DEG
        vx0 -= vx * transfer
        vy0 += vx * transfer * mc.PITCH_UP_Y
        j00 -= transfer
        j10 += transfer * mc.PITCH_UP_Y
        da0 -= vx * d_transfer
        da1 += vx * d_transfer * mc.PITCH_UP_Y

    # Horizontal alignment is applied before drag.
    vx1 = (0.9 * vx0 + 0.1 * vx) * mc.HORIZONTAL_DRAG
    vy1 = vy0 * mc.VERTICAL_DRAG
    j00 = (0.9 * j00 + 0.1) * mc.HORIZONTAL_DRAG
    j01 = 0.9 * j01 * mc.HORIZONTAL_DRAG
    j10 = j10 * mc.VERTICAL_DRAG
    j11 = j11 * mc.VERTICAL_DRAG
    da0 = 0.9 * da0 * mc.HORIZONTAL_DRAG
    da1 = da1 * mc.VERTICAL_DRAG
    return vx1, vy1, j00, j01, j10, j11, da0, da1


@njit(cache=True)
def continuous_fixed_velocity(angles: np.ndarray):
    vx = 0.0
    vy = -0.1
    cycles = 0
    closure = 1.0e30
    for cycle in range(1000):
        old_vx = vx
        old_vy = vy
        for angle in angles:
            vx, vy, _, _, _, _, _, _ = continuous_step_with_jac(vx, vy, angle)
        closure = max(abs(vx - old_vx), abs(vy - old_vy))
        cycles = cycle + 1
        if cycle >= 8 and closure < 1.0e-14:
            break
    return vx, vy, closure, cycles


@njit(cache=True)
def continuous_metrics_and_grad(angles: np.ndarray):
    """Evaluate steady-cycle span/dy and exact local gradients.

    The fixed-point sensitivity is obtained from
    dv*/da = (I - dF/dv)^-1 dF/da, where F is one full cycle.
    """
    n = angles.shape[0]
    fixed_vx, fixed_vy, closure, cycles = continuous_fixed_velocity(angles)

    # Differentiate one cycle F(v, a) at the fixed point.
    p00 = 1.0
    p01 = 0.0
    p10 = 0.0
    p11 = 1.0
    q = np.zeros((2, n), dtype=np.float64)
    vx = fixed_vx
    vy = fixed_vy
    for i in range(n):
        vx, vy, j00, j01, j10, j11, da0, da1 = continuous_step_with_jac(vx, vy, angles[i])
        np00 = j00 * p00 + j01 * p10
        np01 = j00 * p01 + j01 * p11
        np10 = j10 * p00 + j11 * p10
        np11 = j10 * p01 + j11 * p11
        p00, p01, p10, p11 = np00, np01, np10, np11
        for k in range(n):
            old0 = q[0, k]
            old1 = q[1, k]
            q[0, k] = j00 * old0 + j01 * old1
            q[1, k] = j10 * old0 + j11 * old1
        q[0, i] += da0
        q[1, i] += da1

    # Solve (I - dF/dv) S = dF/da for fixed-point velocity sensitivity.
    a00 = 1.0 - p00
    a01 = -p01
    a10 = -p10
    a11 = 1.0 - p11
    det = a00 * a11 - a01 * a10
    sensitivity = np.empty((2, n), dtype=np.float64)
    for k in range(n):
        sensitivity[0, k] = (a11 * q[0, k] - a01 * q[1, k]) / det
        sensitivity[1, k] = (-a10 * q[0, k] + a00 * q[1, k]) / det

    ys = np.empty(n + 1, dtype=np.float64)
    y_jac = np.zeros((n + 1, n), dtype=np.float64)
    x_jac = np.zeros((n + 1, n), dtype=np.float64)
    ys[0] = 0.0
    vx = fixed_vx
    vy = fixed_vy
    x = 0.0
    y = 0.0
    for i in range(n):
        vx, vy, j00, j01, j10, j11, da0, da1 = continuous_step_with_jac(vx, vy, angles[i])
        next_s = np.empty((2, n), dtype=np.float64)
        for k in range(n):
            old0 = sensitivity[0, k]
            old1 = sensitivity[1, k]
            next_s[0, k] = j00 * old0 + j01 * old1
            next_s[1, k] = j10 * old0 + j11 * old1
        next_s[0, i] += da0
        next_s[1, i] += da1
        sensitivity = next_s
        x += vx
        y += vy
        ys[i + 1] = y
        for k in range(n):
            x_jac[i + 1, k] = x_jac[i, k] + sensitivity[0, k]
            y_jac[i + 1, k] = y_jac[i, k] + sensitivity[1, k]

    min_i = 0
    max_i = 0
    for i in range(1, n + 1):
        if ys[i] < ys[min_i]:
            min_i = i
        if ys[i] > ys[max_i]:
            max_i = i
    span = ys[max_i] - ys[min_i]
    span_grad = y_jac[max_i] - y_jac[min_i]
    dy = ys[n]
    dy_grad = y_jac[n].copy()
    return (
        span,
        dy,
        span_grad,
        dy_grad,
        ys[min_i],
        ys[max_i],
        min_i,
        max_i,
        fixed_vx,
        fixed_vy,
        closure,
        cycles,
        ys,
        y_jac,
        x,
        x_jac[n].copy(),
    )


def gradient_check(angles: np.ndarray, indices: list[int], eps: float = 1.0e-4) -> None:
    result = continuous_metrics_and_grad(angles)
    print("gradient check:", flush=True)
    for i in indices:
        xp = angles.copy()
        xm = angles.copy()
        xp[i] += eps
        xm[i] -= eps
        rp = continuous_metrics_and_grad(xp)
        rm = continuous_metrics_and_grad(xm)
        span_fd = (rp[0] - rm[0]) / (2.0 * eps)
        dy_fd = (rp[1] - rm[1]) / (2.0 * eps)
        print(
            f"  i={i:3d} span analytic={result[2][i]:+.8e} fd={span_fd:+.8e} "
            f"dy analytic={result[3][i]:+.8e} fd={dy_fd:+.8e}",
            flush=True,
        )


def optimize_target(
    angles0: np.ndarray,
    split: int,
    target_dy: float,
    maxiter: int,
    ftol: float,
    out_dir: Path,
    unrestricted: bool = False,
):
    if unrestricted:
        lo = np.full(len(angles0), -90.0, dtype=np.float64)
        hi = np.full(len(angles0), 90.0, dtype=np.float64)
    else:
        lo, hi = mc.sign_bounds(len(angles0), split)
    scale = hi - lo
    z0 = np.clip((angles0 - lo) / scale, 0.0, 1.0)
    cache: dict[str, object] = {"z": None, "result": None}
    calls = 0
    started = perf_counter()

    def evaluate(z: np.ndarray):
        nonlocal calls
        z_arr = np.asarray(z, dtype=np.float64)
        cached_z = cache["z"]
        if cached_z is None or not np.array_equal(z_arr, cached_z):
            angles = np.ascontiguousarray(lo + z_arr * scale)
            cache["z"] = z_arr.copy()
            cache["result"] = continuous_metrics_and_grad(angles)
            calls += 1
        return cache["result"]

    def fun(z: np.ndarray) -> float:
        return float(evaluate(z)[0])

    def jac(z: np.ndarray) -> np.ndarray:
        return np.asarray(evaluate(z)[2] * scale, dtype=np.float64)

    def con(z: np.ndarray) -> float:
        return float(evaluate(z)[1] - target_dy)

    def con_jac(z: np.ndarray) -> np.ndarray:
        return np.asarray(evaluate(z)[3] * scale, dtype=np.float64)

    iteration = 0

    def callback(z: np.ndarray) -> None:
        nonlocal iteration
        iteration += 1
        if iteration == 1 or iteration % 10 == 0:
            r = evaluate(z)
            print(
                f"  iter={iteration:4d} calls={calls:5d} span={r[0]:.12f} "
                f"dy={r[1]:+.6e} extrema=({r[6]},{r[7]}) "
                f"elapsed={perf_counter() - started:.1f}s",
                flush=True,
            )

    result = minimize(
        fun,
        z0,
        method="SLSQP",
        jac=jac,
        bounds=[(0.0, 1.0)] * len(z0),
        constraints={"type": "eq", "fun": con, "jac": con_jac},
        callback=callback,
        options={"maxiter": maxiter, "ftol": ftol, "disp": True},
    )
    angles = np.ascontiguousarray(lo + np.asarray(result.x, dtype=np.float64) * scale)
    continuous = continuous_metrics_and_grad(angles)
    start_vx, start_vy = mc.horizon_velocity()
    summary, rows = mc.evaluate_solution(
        f"slsqp_target_{target_dy:g}",
        angles,
        split,
        start_vx,
        start_vy,
        2400,
        bool(result.success),
        str(result.message),
    )
    case_dir = out_dir / f"target_{target_dy:.7f}".replace("-", "neg").replace(".", "p")
    mc.write_solution(case_dir, angles, rows, summary)
    with (case_dir / "optimizer_result.json").open("w", encoding="utf-8") as f:
        json.dump(
            {
                "success": bool(result.success),
                "message": str(result.message),
                "nit": int(result.nit),
                "nfev": int(result.nfev),
                "continuous_span": float(continuous[0]),
                "continuous_dy": float(continuous[1]),
                "continuous_min_tick": int(continuous[6]),
                "continuous_max_tick": int(continuous[7]),
                "java_summary": asdict(summary),
            },
            f,
            ensure_ascii=False,
            indent=2,
        )
    return angles, summary, continuous, result, case_dir


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--split", type=int, default=102)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--targets", default="0,0.0001,0.0003,0.001")
    parser.add_argument("--maxiter", type=int, default=600)
    parser.add_argument("--ftol", type=float, default=1.0e-12)
    parser.add_argument("--unrestricted", action="store_true")
    parser.add_argument("--jitter", type=float, default=0.0)
    parser.add_argument("--zero-jitter", type=float, default=0.0)
    parser.add_argument("--seed", type=int, default=20260710)
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    angles0 = read_angles(args.source)
    if args.jitter > 0.0 or args.zero_jitter > 0.0:
        rng = np.random.default_rng(args.seed)
        if args.jitter > 0.0:
            angles0 += rng.normal(0.0, args.jitter, size=len(angles0))
        if args.zero_jitter > 0.0:
            mask = np.abs(angles0) < 1.0e-3
            angles0[mask] = rng.choice(np.array([-1.0, 1.0]), size=int(mask.sum())) * args.zero_jitter
        angles0 = np.clip(angles0, -90.0, 90.0)
    print("warming up numba...", flush=True)
    _ = continuous_metrics_and_grad(angles0)
    gradient_check(angles0, [1, 22, 80, 106, 130, 160])

    rows = []
    seed = angles0
    best = None
    for target in [float(x) for x in args.targets.split(",") if x.strip()]:
        print(f"\noptimizing target dy={target:+.7f}", flush=True)
        angles, summary, continuous, result, case_dir = optimize_target(
            seed, args.split, target, args.maxiter, args.ftol, args.out_dir, args.unrestricted
        )
        rows.append(
            {
                "target_dy": target,
                "continuous_span": float(continuous[0]),
                "continuous_dy": float(continuous[1]),
                "java_span": summary.height_span,
                "java_dy": summary.dy,
                "success": bool(result.success),
                "case_dir": str(case_dir),
            }
        )
        if summary.dy >= -1.0e-7 and (best is None or summary.height_span < best[1].height_span):
            best = (angles.copy(), summary, case_dir)

    mc.write_csv(args.out_dir / "summary.csv", rows)
    if best is not None:
        angles, summary, source_dir = best
        start_vx, start_vy = mc.horizon_velocity()
        best_summary, best_rows = mc.evaluate_solution(
            "constrained_height_span_slsqp",
            angles,
            args.split,
            start_vx,
            start_vy,
            2400,
            True,
            f"selected from {source_dir}",
        )
        mc.write_solution(args.out_dir / "best", angles, best_rows, best_summary)
        print("\nBEST JAVA-EXACT FEASIBLE")
        print(json.dumps(asdict(best_summary), ensure_ascii=False, indent=2))
    else:
        print("\nNo Java-exact feasible target found.")


if __name__ == "__main__":
    main()
