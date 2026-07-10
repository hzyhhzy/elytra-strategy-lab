from __future__ import annotations

import argparse
import csv
import json
from dataclasses import asdict
from pathlib import Path
from time import perf_counter

import numpy as np
from scipy.interpolate import CubicSpline
from scipy.ndimage import gaussian_filter1d
from scipy.optimize import minimize
from skimage.restoration import denoise_tv_chambolle

import min_height_span_periodic as mc


def read_angles(path: Path) -> np.ndarray:
    with path.open(newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    return np.array([float(row["angle"]) for row in rows], dtype=np.float64)


def segment_basis(length: int, spacing: int) -> np.ndarray:
    count = max(4, int(np.ceil((length - 1) / spacing)) + 1)
    knots = np.linspace(0.0, length - 1.0, count)
    identity = np.eye(count, dtype=np.float64)
    spline = CubicSpline(knots, identity, axis=0, bc_type="natural")
    return np.asarray(spline(np.arange(length, dtype=np.float64)), dtype=np.float64)


def correction_basis(period: int, split: int, spacing: int) -> np.ndarray:
    first = segment_basis(split, spacing)
    second = segment_basis(period - split, spacing)
    basis = np.zeros((period, first.shape[1] + second.shape[1]), dtype=np.float64)
    basis[:split, : first.shape[1]] = first
    basis[split:, first.shape[1] :] = second
    return basis


def roughness(angles: np.ndarray) -> tuple[float, float]:
    delta = np.diff(np.r_[angles, angles[0]])
    return float(np.sqrt(np.mean(delta * delta))), float(np.mean(np.abs(delta)))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--split", type=int, required=True)
    parser.add_argument("--mode", choices=("horizontal", "climb"), required=True)
    parser.add_argument("--sigma", type=float, default=0.0)
    parser.add_argument("--filter", choices=("gaussian", "tv"), default="gaussian")
    parser.add_argument("--tv-weight", type=float, default=30.0)
    parser.add_argument("--spacing", type=int, default=8)
    parser.add_argument("--coeff-bound", type=float, default=12.0)
    parser.add_argument("--fd-eps", type=float, default=0.1)
    parser.add_argument("--maxiter", type=int, default=300)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    raw = read_angles(args.source)
    lo, hi = mc.sign_bounds(len(raw), args.split)
    if args.filter == "tv":
        base = denoise_tv_chambolle(
            raw, weight=args.tv_weight, eps=1.0e-7, max_num_iter=2000
        )
    else:
        base = gaussian_filter1d(raw, args.sigma, mode="wrap")
    base = np.clip(base, lo, hi)
    basis = correction_basis(len(raw), args.split, args.spacing)
    coefficients = np.zeros(basis.shape[1], dtype=np.float64)
    start_vx, start_vy = mc.horizon_velocity()
    factor = 20.0 / len(raw)
    started = perf_counter()
    cache_x = None
    cache_value = None

    def decode(x: np.ndarray) -> np.ndarray:
        return np.ascontiguousarray(np.clip(base + basis @ x, lo, hi))

    def evaluate(x: np.ndarray) -> tuple[float, float]:
        nonlocal cache_x, cache_value
        x = np.asarray(x, dtype=np.float64)
        if cache_x is None or not np.array_equal(x, cache_x):
            angles = decode(x)
            result = mc.eval_converged(angles, 2400, start_vx, start_vy)
            cache_x = x.copy()
            cache_value = (float(result[0]), float(result[1]))
        return cache_value

    def values_and_jac(x: np.ndarray):
        dx, dy = evaluate(x)
        grad_dx = np.empty_like(x)
        grad_dy = np.empty_like(x)
        for i in range(len(x)):
            xp = x.copy()
            xm = x.copy()
            xp[i] += args.fd_eps
            xm[i] -= args.fd_eps
            dxp, dyp = evaluate(xp)
            dxm, dym = evaluate(xm)
            grad_dx[i] = (dxp - dxm) / (2.0 * args.fd_eps)
            grad_dy[i] = (dyp - dym) / (2.0 * args.fd_eps)
        return dx, dy, grad_dx, grad_dy

    last_x = None
    last_values = None

    def cached_with_jac(x: np.ndarray):
        nonlocal last_x, last_values
        if last_x is None or not np.array_equal(x, last_x):
            last_x = np.asarray(x, dtype=np.float64).copy()
            last_values = values_and_jac(last_x)
        return last_values

    def objective(x: np.ndarray) -> float:
        dx, dy = evaluate(x)
        return -factor * (dx if args.mode == "horizontal" else dy)

    def objective_jac(x: np.ndarray) -> np.ndarray:
        _, _, grad_dx, grad_dy = cached_with_jac(x)
        return -factor * (grad_dx if args.mode == "horizontal" else grad_dy)

    iteration = 0

    def callback(x: np.ndarray) -> None:
        nonlocal iteration
        iteration += 1
        if iteration == 1 or iteration % 10 == 0:
            dx, dy = evaluate(x)
            rms, tv = roughness(decode(x))
            print(
                f"iter={iteration:03d} horizontal={factor * dx:.9f} "
                f"climb={factor * dy:.9f} dy={dy:+.9g} rms={rms:.4f} "
                f"tv={tv:.4f} elapsed={perf_counter() - started:.1f}s",
                flush=True,
            )

    bounds = [(-args.coeff_bound, args.coeff_bound)] * len(coefficients)
    if args.mode == "horizontal":
        result = minimize(
            objective,
            coefficients,
            method="SLSQP",
            jac=objective_jac,
            bounds=bounds,
            constraints={
                "type": "ineq",
                "fun": lambda x: evaluate(x)[1],
                "jac": lambda x: cached_with_jac(x)[3],
            },
            callback=callback,
            options={"maxiter": args.maxiter, "ftol": 1.0e-11, "disp": True},
        )
    else:
        result = minimize(
            objective,
            coefficients,
            method="SLSQP",
            jac=objective_jac,
            bounds=bounds,
            callback=callback,
            options={"maxiter": args.maxiter, "ftol": 1.0e-11, "disp": True},
        )

    angles = decode(result.x)
    summary, rows = mc.evaluate_solution(
        f"smooth_{args.mode}", angles, args.split, start_vx, start_vy, 2400,
        bool(result.success), str(result.message),
    )
    mc.write_solution(args.out_dir / "best", angles, rows, summary)
    rms, tv = roughness(angles)
    details = {
        "mode": args.mode,
        "sigma": args.sigma,
        "filter": args.filter,
        "tv_weight": args.tv_weight,
        "spacing": args.spacing,
        "coefficient_count": int(len(coefficients)),
        "coefficient_bound": args.coeff_bound,
        "fd_eps": args.fd_eps,
        "rms_delta_deg_circular": rms,
        "mean_abs_delta_deg_circular": tv,
        "optimizer_success": bool(result.success),
        "optimizer_message": str(result.message),
        "iterations": int(result.nit),
        "elapsed_seconds": perf_counter() - started,
        "summary": asdict(summary),
    }
    with (args.out_dir / "optimization.json").open("w", encoding="utf-8") as f:
        json.dump(details, f, ensure_ascii=False, indent=2)
    print(json.dumps(details, ensure_ascii=False, indent=2), flush=True)


if __name__ == "__main__":
    main()
