from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from dataclasses import asdict
from pathlib import Path
from time import perf_counter

import numpy as np
from scipy.optimize import minimize


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
if (PROJECT_ROOT / "results").is_dir():
    REPO = PROJECT_ROOT
    WORKSPACE = PROJECT_ROOT.parent
    DEFAULT_OUT_ROOT = REPO / "scratch"
else:
    WORKSPACE = PROJECT_ROOT
    REPO = WORKSPACE / "elytra-strategy-lab"
    DEFAULT_OUT_ROOT = WORKSPACE / "analysis"
sys.path.insert(0, str(SCRIPT_DIR))

try:
    import continuous_fixed_point_gradient as continuous  # noqa: E402
except ModuleNotFoundError:
    import constrained_height_span_slsqp as continuous  # noqa: E402
import min_height_span_periodic as mc  # noqa: E402


DEFAULT_SMOOTH = REPO / "results" / "fastest-climb-rate" / "waveform.csv"
DEFAULT_RAW = REPO / "results" / "lbfgsb-max-climb-raw" / "waveform.csv"
DEFAULT_OUT = DEFAULT_OUT_ROOT / "no_chatter_reversal_regularized_climb"


def read_angles(path: Path) -> np.ndarray:
    with path.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise ValueError(f"empty waveform: {path}")
    if "angle" in rows[0]:
        return np.array([float(row["angle"]) for row in rows], dtype=np.float64)
    if "angleDeg_pass_to_stepElytra2D" in rows[0]:
        return np.array(
            [float(row["angleDeg_pass_to_stepElytra2D"]) for row in rows],
            dtype=np.float64,
        )
    if "pitchDeg_internal" in rows[0]:
        return -np.array([float(row["pitchDeg_internal"]) for row in rows], dtype=np.float64)
    raise ValueError(f"no recognized angle column in {path}")


def reversal_penalty_and_grad(
    angles: np.ndarray,
    eps: float,
    scale: float,
    lags: tuple[int, ...],
) -> tuple[float, np.ndarray]:
    """Robustly penalize alternating slopes while leaving jumps inexpensive.

    For circular differences d, a reversal has d[i] * d[i+lag] < 0.  The
    log1p transform makes each reversal grow slowly with amplitude, so a real
    phase jump is allowed while a long up/down/up/down train remains costly.
    """
    n = len(angles)
    d = np.roll(angles, -1) - angles
    grad_d = np.zeros(n, dtype=np.float64)
    value = 0.0
    for lag in lags:
        d_next = np.roll(d, -lag)
        product = d * d_next
        root = np.sqrt(product * product + eps * eps)
        negative_part = 0.5 * (root - product)
        term = np.log1p(negative_part / scale)
        value += float(np.mean(term))

        dq_dp = 0.5 * (product / root - 1.0)
        dr_dp = dq_dp / (scale + negative_part)
        grad_d += dr_dp * d_next / n
        grad_d += np.roll(dr_dp * d, lag) / n

    value /= len(lags)
    grad_d /= len(lags)
    # d[i] = angle[i+1] - angle[i], including the circular edge.
    grad_angles = np.roll(grad_d, 1) - grad_d
    return value, grad_angles


def chatter_metrics(angles: np.ndarray, threshold: float = 0.5) -> dict[str, float | int]:
    d = np.roll(angles, -1) - angles
    products = d * np.roll(d, -1)
    significant = d[np.abs(d) >= threshold]
    if len(significant) > 1:
        significant_changes = int(np.sum(significant * np.roll(significant, -1) < 0.0))
    else:
        significant_changes = 0
    return {
        "adjacentReversalsProductBelow0p25": int(np.sum(products < -0.25)),
        "adjacentReversalsProductBelow1": int(np.sum(products < -1.0)),
        "significantDirectionChangesCircular": significant_changes,
        "meanAbsCircularDeltaDegrees": float(np.mean(np.abs(d))),
        "rmsCircularDeltaDegrees": float(np.sqrt(np.mean(d * d))),
        "maxAbsCircularDeltaDegrees": float(np.max(np.abs(d))),
    }


class Objective:
    def __init__(
        self,
        period: int,
        regularization: float,
        penalty_eps: float,
        penalty_scale: float,
        lags: tuple[int, ...],
    ) -> None:
        self.period = period
        self.regularization = regularization
        self.penalty_eps = penalty_eps
        self.penalty_scale = penalty_scale
        self.lags = lags
        self.calls = 0
        self.best_value = math.inf
        self.best_angles: np.ndarray | None = None

    def evaluate(self, angles: np.ndarray) -> tuple[float, np.ndarray, tuple, float]:
        x = np.ascontiguousarray(angles, dtype=np.float64)
        metrics = continuous.continuous_metrics_and_grad(x)
        climb = float(metrics[1] * 20.0 / self.period)
        climb_grad = np.asarray(metrics[3], dtype=np.float64) * (20.0 / self.period)
        penalty, penalty_grad = reversal_penalty_and_grad(
            x, self.penalty_eps, self.penalty_scale, self.lags
        )
        value = -climb + self.regularization * penalty
        grad = -climb_grad + self.regularization * penalty_grad
        self.calls += 1
        if value < self.best_value:
            self.best_value = value
            self.best_angles = x.copy()
        return float(value), grad, metrics, penalty


def java_evaluate(name: str, angles: np.ndarray, message: str):
    start_vx, start_vy = mc.horizon_velocity()
    return mc.evaluate_solution(
        name,
        np.ascontiguousarray(angles, dtype=np.float64),
        -1,
        start_vx,
        start_vy,
        3000,
        True,
        message,
    )


def optimize_one(
    seed: np.ndarray,
    source_name: str,
    regularization: float,
    out_dir: Path,
    args: argparse.Namespace,
) -> tuple[np.ndarray, dict]:
    period = len(seed)
    objective = Objective(
        period,
        regularization,
        args.penalty_eps,
        args.penalty_scale,
        tuple(args.lags),
    )
    started = perf_counter()
    iteration = 0
    start_vx, start_vy = mc.horizon_velocity()

    def exact_score(x: np.ndarray) -> tuple[float, float, float]:
        metrics = mc.eval_converged(
            np.ascontiguousarray(x, dtype=np.float64), 3000, start_vx, start_vy
        )
        climb = float(metrics[1] * 20.0 / period)
        penalty, _ = reversal_penalty_and_grad(
            x, args.penalty_eps, args.penalty_scale, tuple(args.lags)
        )
        if args.max_direction_changes >= 0:
            changes = chatter_metrics(x, args.direction_threshold)[
                "significantDirectionChangesCircular"
            ]
            if changes > args.max_direction_changes:
                return math.inf, climb, penalty
        return -climb + regularization * penalty, climb, penalty

    initial_exact_value, initial_exact_climb, initial_exact_penalty = exact_score(seed)
    best_exact = {
        "value": initial_exact_value,
        "climb": initial_exact_climb,
        "penalty": initial_exact_penalty,
        "angles": np.asarray(seed, dtype=np.float64).copy(),
        "iteration": 0,
    }

    def fun_jac(x: np.ndarray):
        value, grad, _, _ = objective.evaluate(x)
        return value, grad

    def callback(x: np.ndarray) -> None:
        nonlocal iteration
        iteration += 1
        exact_value, exact_climb, exact_penalty = exact_score(x)
        if exact_value < best_exact["value"]:
            best_exact.update(
                value=exact_value,
                climb=exact_climb,
                penalty=exact_penalty,
                angles=np.asarray(x, dtype=np.float64).copy(),
                iteration=iteration,
            )
        if iteration == 1 or iteration % args.print_every == 0:
            value, _, metrics, penalty = objective.evaluate(x)
            climb = float(metrics[1] * 20.0 / period)
            chatter = chatter_metrics(x)
            print(
                f"  iter={iteration:4d} calls={objective.calls:5d} "
                f"climb={climb:.9f} penalty={penalty:.6f} "
                f"rev={chatter['adjacentReversalsProductBelow1']} "
                f"java={exact_climb:.9f} javaBest={best_exact['climb']:.9f} "
                f"obj={value:.9f} elapsed={perf_counter() - started:.1f}s",
                flush=True,
            )

    result = minimize(
        fun_jac,
        np.clip(np.asarray(seed, dtype=np.float64), -89.99, 89.99),
        method="L-BFGS-B",
        jac=True,
        bounds=[(-89.99, 89.99)] * period,
        callback=callback,
        options={
            "maxiter": args.maxiter,
            "maxfun": args.maxfun,
            "maxls": args.maxls,
            "maxcor": args.maxcor,
            "ftol": args.ftol,
            "gtol": args.gtol,
        },
    )
    continuous_angles = (
        objective.best_angles.copy()
        if objective.best_angles is not None
        else np.asarray(result.x, dtype=np.float64)
    )
    for candidate_iteration, candidate in [
        (int(result.nit), np.asarray(result.x, dtype=np.float64)),
        (int(result.nit), continuous_angles),
    ]:
        exact_value, exact_climb, exact_penalty = exact_score(candidate)
        if exact_value < best_exact["value"]:
            best_exact.update(
                value=exact_value,
                climb=exact_climb,
                penalty=exact_penalty,
                angles=candidate.copy(),
                iteration=candidate_iteration,
            )
    angles = np.asarray(best_exact["angles"], dtype=np.float64)

    coordinate_calls = 0
    coordinate_accepts = 0
    for step in args.coordinate_steps:
        for sweep in range(args.coordinate_sweeps):
            accepts_this_sweep = 0
            for i in range(period):
                current_value = float(best_exact["value"])
                best_trial: tuple[float, float, float, np.ndarray] | None = None
                for direction in (-1.0, 1.0):
                    trial_value = float(np.clip(angles[i] + direction * step, -89.99, 89.99))
                    if trial_value == angles[i]:
                        continue
                    trial = angles.copy()
                    trial[i] = trial_value
                    score, climb, trial_penalty = exact_score(trial)
                    coordinate_calls += 1
                    if score < current_value - args.coordinate_tolerance:
                        if best_trial is None or score < best_trial[0]:
                            best_trial = (score, climb, trial_penalty, trial)
                if best_trial is not None:
                    score, climb, trial_penalty, angles = best_trial
                    best_exact.update(
                        value=score,
                        climb=climb,
                        penalty=trial_penalty,
                        angles=angles.copy(),
                        iteration=int(result.nit),
                    )
                    accepts_this_sweep += 1
                    coordinate_accepts += 1
            print(
                f"  coordinate step={step:g} sweep={sweep + 1} "
                f"accepts={accepts_this_sweep} java={best_exact['climb']:.9f} "
                f"penalty={best_exact['penalty']:.6f}",
                flush=True,
            )
            if accepts_this_sweep == 0:
                break
    value, _, continuous_metrics, penalty = objective.evaluate(angles)
    java_summary, rows = java_evaluate(
        f"{source_name}_lambda_{regularization:g}",
        angles,
        str(result.message),
    )
    tag = f"lambda_{regularization:.8g}".replace(".", "p")
    case_dir = out_dir / source_name / tag
    mc.write_solution(case_dir, angles, rows, java_summary)
    chatter = chatter_metrics(angles)
    record = {
        "source": source_name,
        "regularization": regularization,
        "penaltyEps": args.penalty_eps,
        "penaltyScale": args.penalty_scale,
        "lags": list(args.lags),
        "objective": value,
        "reversalPenalty": penalty,
        "continuousClimbBlocksPerSecond": float(continuous_metrics[1] * 20.0 / period),
        "continuousDyBlocksPerCycle": float(continuous_metrics[1]),
        "javaClimbBlocksPerSecond": float(java_summary.dy * 20.0 / period),
        "javaSummary": asdict(java_summary),
        "chatter": chatter,
        "optimizer": {
            "success": bool(result.success),
            "status": int(result.status),
            "message": str(result.message),
            "nit": int(result.nit),
            "nfev": int(result.nfev),
            "njev": int(result.njev),
            "callsIncludingCallbacks": objective.calls,
            "javaBestIteration": int(best_exact["iteration"]),
            "javaRegularizedObjective": float(best_exact["value"]),
            "coordinateCalls": coordinate_calls,
            "coordinateAccepts": coordinate_accepts,
            "elapsedSeconds": perf_counter() - started,
        },
        "caseDirectory": str(case_dir),
    }
    with (case_dir / "optimizer_result.json").open("w", encoding="utf-8") as f:
        json.dump(record, f, ensure_ascii=False, indent=2)
    print(
        f"  JAVA climb={record['javaClimbBlocksPerSecond']:.9f} "
        f"dy={java_summary.dy:+.9f} rev={chatter['adjacentReversalsProductBelow1']} "
        f"directions={chatter['significantDirectionChangesCircular']}",
        flush=True,
    )
    return angles, record


def write_summary(path: Path, records: list[dict]) -> None:
    rows = []
    for record in records:
        summary = record["javaSummary"]
        chatter = record["chatter"]
        rows.append(
            {
                "source": record["source"],
                "lambda": record["regularization"],
                "period": summary["period"],
                "java_climb_bps": record["javaClimbBlocksPerSecond"],
                "java_dy": summary["dy"],
                "java_avg_horizontal_bps": summary["avg_horizontal_bps"],
                "continuous_climb_bps": record["continuousClimbBlocksPerSecond"],
                "reversal_penalty": record["reversalPenalty"],
                "adjacent_reversals": chatter["adjacentReversalsProductBelow1"],
                "significant_direction_changes": chatter["significantDirectionChangesCircular"],
                "rms_circular_delta_deg": chatter["rmsCircularDeltaDegrees"],
                "max_circular_delta_deg": chatter["maxAbsCircularDeltaDegrees"],
                "nit": record["optimizer"]["nit"],
                "success": record["optimizer"]["success"],
                "case_directory": record["caseDirectory"],
            }
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def parse_float_list(text: str) -> list[float]:
    return [float(item) for item in text.split(",") if item.strip()]


def parse_int_list(text: str) -> list[int]:
    values = tuple(int(item) for item in text.split(",") if item.strip())
    if not values or any(value < 1 for value in values):
        raise ValueError("lags must be positive integers")
    return list(values)


def resample_periodic(angles: np.ndarray, period: int) -> np.ndarray:
    if period == len(angles):
        return angles.copy()
    old_x = np.arange(len(angles) + 1, dtype=np.float64) / len(angles)
    old_y = np.r_[angles, angles[0]]
    new_x = np.arange(period, dtype=np.float64) / period
    return np.interp(new_x, old_x, old_y).astype(np.float64)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--smooth-source", type=Path, default=DEFAULT_SMOOTH)
    parser.add_argument("--raw-source", type=Path, default=DEFAULT_RAW)
    parser.add_argument("--sources", choices=["smooth", "raw", "both"], default="both")
    parser.add_argument("--lambdas", default="0.03,0.02,0.015,0.012,0.01,0.008,0.005,0.002")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--penalty-eps", type=float, default=1.0)
    parser.add_argument("--penalty-scale", type=float, default=25.0)
    parser.add_argument("--lags", type=parse_int_list, default=parse_int_list("1,2,3"))
    parser.add_argument("--maxiter", type=int, default=700)
    parser.add_argument("--maxfun", type=int, default=20000)
    parser.add_argument("--maxls", type=int, default=40)
    parser.add_argument("--maxcor", type=int, default=30)
    parser.add_argument("--ftol", type=float, default=1.0e-13)
    parser.add_argument("--gtol", type=float, default=1.0e-8)
    parser.add_argument("--print-every", type=int, default=10)
    parser.add_argument("--periods", type=parse_int_list, default=[])
    parser.add_argument(
        "--coordinate-steps",
        type=parse_float_list,
        default=parse_float_list("1,0.3,0.1,0.03,0.01,0.003"),
    )
    parser.add_argument("--coordinate-sweeps", type=int, default=2)
    parser.add_argument("--coordinate-tolerance", type=float, default=1.0e-13)
    parser.add_argument("--max-direction-changes", type=int, default=-1)
    parser.add_argument("--direction-threshold", type=float, default=0.5)
    args = parser.parse_args()

    lambdas = parse_float_list(args.lambdas)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    sources: list[tuple[str, Path]] = []
    if args.sources in ("smooth", "both"):
        sources.append(("smooth_seed", args.smooth_source))
    if args.sources in ("raw", "both"):
        sources.append(("raw_seed", args.raw_source))

    print("warming up Numba continuous fixed-point gradient...", flush=True)
    _ = continuous.continuous_metrics_and_grad(read_angles(sources[0][1]))
    records: list[dict] = []
    for source_name, source_path in sources:
        source_angles = read_angles(source_path)
        periods = args.periods or [len(source_angles)]
        for period in periods:
            seed = resample_periodic(source_angles, period)
            case_source_name = f"{source_name}_T{period}" if len(periods) > 1 else source_name
            print(
                f"\nSOURCE {case_source_name}: {source_path} ({len(seed)} ticks)",
                flush=True,
            )
            baseline_summary, _ = java_evaluate(
                f"{case_source_name}_baseline", seed, "baseline"
            )
            baseline_penalty, _ = reversal_penalty_and_grad(
                seed, args.penalty_eps, args.penalty_scale, tuple(args.lags)
            )
            print(
                f"baseline Java climb={baseline_summary.dy * 20.0 / len(seed):.9f} "
                f"penalty={baseline_penalty:.6f} chatter={chatter_metrics(seed)}",
                flush=True,
            )
            for regularization in lambdas:
                print(f"\nlambda={regularization:g}", flush=True)
                seed, record = optimize_one(
                    seed, case_source_name, regularization, args.out_dir, args
                )
                records.append(record)
                write_summary(args.out_dir / "summary.csv", records)

    print(f"\nsummary: {args.out_dir / 'summary.csv'}", flush=True)


if __name__ == "__main__":
    main()
