from __future__ import annotations

import argparse
import csv
import json
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np
from scipy.optimize import Bounds, LinearConstraint, milp
from scipy.sparse import lil_matrix

import min_height_span_periodic as mc


ROOT = Path(__file__).resolve().parent.parent


def read_angles(path: Path) -> np.ndarray:
    with path.open(newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise ValueError(f"empty waveform: {path}")
    field = "angle" if "angle" in rows[0] else "angleDeg_pass_to_stepElytra2D"
    return np.array([float(row[field]) for row in rows], dtype=np.float64)


@dataclass
class Metrics:
    dx: float
    dy: float
    converged: bool


@dataclass
class Move:
    index: int
    value: float
    ddx: float
    ddy: float


def metrics(angles: np.ndarray, start_vx: float, start_vy: float) -> Metrics:
    result = mc.eval_converged(angles, 2400, start_vx, start_vy)
    return Metrics(float(result[0]), float(result[1]), bool(result[9]))


def enumerate_moves(
    angles: np.ndarray,
    lo: np.ndarray,
    hi: np.ndarray,
    step: float,
    base: Metrics,
    start_vx: float,
    start_vy: float,
    include_endpoints: bool,
) -> list[Move]:
    moves: list[Move] = []
    for i in range(len(angles)):
        values = [angles[i] - step, angles[i] + step]
        if include_endpoints:
            values.extend((lo[i], hi[i]))
        for raw_value in sorted(set(values)):
            value = float(np.clip(raw_value, lo[i], hi[i]))
            if value == angles[i]:
                continue
            trial = angles.copy()
            trial[i] = value
            result = metrics(trial, start_vx, start_vy)
            if not result.converged:
                continue
            ddx = result.dx - base.dx
            ddy = result.dy - base.dy
            # Many sub-cell angle changes map to exactly the same Mth lookup values.
            if abs(ddx) < 1.0e-14 and abs(ddy) < 1.0e-14:
                continue
            moves.append(Move(i, value, ddx, ddy))
    return moves


def solve_move_set(
    moves: list[Move],
    base: Metrics,
    tick_count: int,
    mode: str,
    cardinality: int,
    dy_margin: float,
    time_limit: float,
):
    move_count = len(moves)
    rows = 1 + tick_count
    matrix = lil_matrix((rows, move_count), dtype=np.float64)
    lower = np.full(rows, -np.inf, dtype=np.float64)
    upper = np.full(rows, np.inf, dtype=np.float64)
    upper[0] = float(cardinality)
    upper[1:] = 1.0
    for column, move in enumerate(moves):
        matrix[0, column] = 1.0
        matrix[1 + move.index, column] = 1.0

    constraints = [LinearConstraint(matrix.tocsr(), lower, upper)]
    if mode == "horizontal":
        dy_row = np.array([move.ddy for move in moves], dtype=np.float64)[None, :]
        constraints.append(
            LinearConstraint(dy_row, -base.dy + dy_margin, np.inf)
        )
        objective = -np.array([move.ddx for move in moves], dtype=np.float64)
    else:
        objective = -np.array([move.ddy for move in moves], dtype=np.float64)

    result = milp(
        c=objective,
        integrality=np.ones(move_count, dtype=np.int8),
        bounds=Bounds(np.zeros(move_count), np.ones(move_count)),
        constraints=constraints,
        options={"time_limit": time_limit, "mip_rel_gap": 0.0, "presolve": True},
    )
    if result.x is None:
        return None
    return [i for i, value in enumerate(result.x) if value > 0.5]


def better(candidate: Metrics, current: Metrics, mode: str) -> bool:
    if not candidate.converged:
        return False
    if mode == "horizontal":
        return candidate.dy >= 0.0 and candidate.dx > current.dx + 1.0e-12
    return candidate.dy > current.dy + 1.0e-12


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--split", type=int, required=True)
    parser.add_argument("--mode", choices=["horizontal", "climb"], required=True)
    parser.add_argument("--steps", default="3,1,0.3,0.1,0.03,0.01,0.003")
    parser.add_argument("--cardinalities", default="2,4,8,16,32,64,128,256")
    parser.add_argument("--margins", default="0,1e-8,1e-7,1e-6,1e-5,1e-4")
    parser.add_argument("--passes", type=int, default=12)
    parser.add_argument("--time-limit", type=float, default=10.0)
    parser.add_argument("--bound-margin", type=float, default=0.0)
    parser.add_argument("--include-endpoints", action="store_true")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    angles = read_angles(args.source)
    lo, hi = mc.sign_bounds(len(angles), args.split)
    lo = np.where(lo < 0.0, lo + args.bound_margin, lo)
    hi = np.where(hi > 0.0, hi - args.bound_margin, hi)
    angles = np.clip(angles, lo, hi)
    start_vx, start_vy = mc.horizon_velocity()
    base = metrics(angles, start_vx, start_vy)
    seconds = len(angles) / 20.0
    print(
        f"initial horizontal={base.dx / seconds:.12f} climb={base.dy / seconds:.12f} "
        f"dy={base.dy:+.12g}", flush=True
    )

    cardinalities = [int(x) for x in args.cardinalities.split(",") if x.strip()]
    margins = [float(x) for x in args.margins.split(",") if x.strip()]
    progress: list[dict] = []
    for step in [float(x) for x in args.steps.split(",") if x.strip()]:
        for pass_index in range(args.passes):
            moves = enumerate_moves(
                angles, lo, hi, step, base, start_vx, start_vy,
                args.include_endpoints,
            )
            winner = None
            for cardinality in cardinalities:
                trial_margins = margins if args.mode == "horizontal" else [0.0]
                for margin in trial_margins:
                    selected = solve_move_set(
                        moves, base, len(angles), args.mode, cardinality, margin,
                        args.time_limit,
                    )
                    if not selected:
                        continue
                    trial = angles.copy()
                    for move_index in selected:
                        move = moves[move_index]
                        trial[move.index] = move.value
                    actual = metrics(trial, start_vx, start_vy)
                    if not better(actual, base, args.mode):
                        continue
                    score = actual.dx if args.mode == "horizontal" else actual.dy
                    if winner is None or score > winner[1]:
                        winner = (trial, score, actual, selected, cardinality, margin)

            if winner is None:
                print(
                    f"step={step:g} pass={pass_index} moves={len(moves)} no improvement",
                    flush=True,
                )
                break
            angles, _, base, selected, cardinality, margin = winner
            row = {
                "step": step,
                "pass": pass_index,
                "selected": len(selected),
                "cardinality_limit": cardinality,
                "margin": margin,
                "dx": base.dx,
                "dy": base.dy,
                "avg_horizontal_bps": base.dx / seconds,
                "avg_climb_bps": base.dy / seconds,
            }
            progress.append(row)
            print(
                f"step={step:g} pass={pass_index} selected={len(selected)} K={cardinality} "
                f"horizontal={row['avg_horizontal_bps']:.12f} "
                f"climb={row['avg_climb_bps']:.12f} dy={base.dy:+.12g}",
                flush=True,
            )

    summary, rows = mc.evaluate_solution(
        f"milp_exact_{args.mode}", angles, args.split, start_vx, start_vy, 2400,
        True, "Java-exact quantization-aware MILP move selection",
    )
    mc.write_solution(args.out_dir / "best", angles, rows, summary)
    mc.write_csv(args.out_dir / "progress.csv", progress)
    with (args.out_dir / "summary.json").open("w", encoding="utf-8") as f:
        json.dump(asdict(summary), f, ensure_ascii=False, indent=2)
    print("BEST")
    print(json.dumps(asdict(summary), ensure_ascii=False, indent=2), flush=True)


if __name__ == "__main__":
    main()
