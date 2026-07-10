from __future__ import annotations

import argparse
import csv
import json
from dataclasses import asdict
from pathlib import Path

import numpy as np

import min_height_span_periodic as mc
from milp_exact_objective_refine import metrics, read_angles


def candidate_score(dx: float, dy: float, period: int, mode: str) -> float:
    seconds = period / 20.0
    if mode == "climb":
        return dy / seconds
    horizontal = dx / seconds
    return horizontal + 2000.0 * min(0.0, dy / seconds)


def candidates(angles: np.ndarray, split: int, direction: int):
    n = len(angles)
    if direction < 0:
        for i in range(n):
            new_split = split - int(i < split)
            if 0 < new_split < n - 1:
                yield np.delete(angles, i), new_split, f"delete_{i}"
        return

    for i in range(n + 1):
        new_split = split + int(i <= split)
        if not (0 < new_split < n + 1):
            continue
        lo, hi = mc.sign_bounds(n + 1, new_split)
        left = angles[(i - 1) % n]
        right = angles[i % n]
        values = (left, right, 0.5 * (left + right), lo[i], hi[i])
        for value_index, value in enumerate(values):
            seed = np.insert(angles, i, np.clip(value, lo[i], hi[i]))
            yield seed, new_split, f"insert_{i}_{value_index}"


def write_waveform(path: Path, angles: np.ndarray) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(("tick", "angle"))
        writer.writerows((i, f"{value:.17g}") for i, value in enumerate(angles))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--split", type=int, required=True)
    parser.add_argument("--mode", choices=("horizontal", "climb"), required=True)
    parser.add_argument("--direction", choices=("delete", "insert", "both"), default="both")
    parser.add_argument("--keep", type=int, default=20)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    source = read_angles(args.source)
    start_vx, start_vy = mc.horizon_velocity()
    directions = []
    if args.direction in ("delete", "both"):
        directions.append(-1)
    if args.direction in ("insert", "both"):
        directions.append(1)

    ranked = []
    for direction in directions:
        for angles, split, name in candidates(source, args.split, direction):
            result = metrics(angles, start_vx, start_vy)
            score = candidate_score(result.dx, result.dy, len(angles), args.mode)
            ranked.append((score, angles, split, name, result))
    ranked.sort(key=lambda item: item[0], reverse=True)

    records = []
    for rank, (score, angles, split, name, result) in enumerate(ranked[: args.keep], 1):
        case_dir = args.out_dir / f"rank_{rank:02d}_{name}"
        case_dir.mkdir(parents=True, exist_ok=True)
        write_waveform(case_dir / "waveform.csv", angles)
        summary, rows = mc.evaluate_solution(
            name, angles, split, start_vx, start_vy, 2400, True,
            "one-frame exact structural continuation",
        )
        mc.write_solution(case_dir, angles, rows, summary)
        record = {
            "rank": rank,
            "source": name,
            "period": len(angles),
            "split": split,
            "score": score,
            "horizontal_bps": summary.avg_horizontal_bps,
            "climb_bps": summary.dy / (len(angles) / 20.0),
            "dy": summary.dy,
            "feasible": summary.dy >= 0.0,
            "case_dir": str(case_dir),
        }
        records.append(record)
        print(json.dumps(record, ensure_ascii=False), flush=True)
    mc.write_csv(args.out_dir / "summary.csv", records)


if __name__ == "__main__":
    main()
