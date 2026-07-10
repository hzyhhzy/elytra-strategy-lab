from __future__ import annotations

import csv
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results"
TARGETS = (
    "periodic-vx025-no-drop",
    "lbfgsb-max-climb-raw",
    "fastest-climb-rate",
    "fastest-horizontal-speed",
    "fastest-horizontal-speed-smooth",
)


def read_rows(path: Path) -> list[dict[str, float]]:
    with path.open(newline="", encoding="utf-8-sig") as file:
        return [
            {key: float(value) for key, value in row.items()}
            for row in csv.DictReader(file)
        ]


def write_rows(path: Path, rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def rotate_result(name: str) -> int:
    result_dir = RESULTS / name
    phase_path = result_dir / "phase.json"
    if phase_path.exists():
        phase_info = json.loads(phase_path.read_text(encoding="utf-8"))
        if phase_info.get("highestPointAtTickZero"):
            return int(phase_info.get("rotationTicks", 0))
    waveform = read_rows(result_dir / "waveform.csv")
    trajectory = read_rows(result_dir / "trajectory.csv")
    period = len(waveform)
    if len(trajectory) != period:
        raise ValueError(f"{name}: waveform/trajectory length mismatch")

    x = [0.0] + [row["x"] for row in trajectory]
    y = [0.0] + [row["y"] for row in trajectory]
    vx = [trajectory[-1]["vx"]] + [row["vx"] for row in trajectory]
    vy = [trajectory[-1]["vy"]] + [row["vy"] for row in trajectory]
    highest = max(range(period + 1), key=lambda index: y[index])
    phase = 0 if highest == period else highest
    if phase == 0:
        rotation = {
            "highestPointAtTickZero": True,
            "rotationTicks": 0,
            "highestStateIndexBeforeRotation": highest,
        }
        phase_path.write_text(
            json.dumps(rotation, indent=2) + "\n", encoding="utf-8"
        )
        return 0

    angles = [row["angle"] for row in waveform]
    rotated_angles = angles[phase:] + angles[:phase]
    rotated_waveform = [
        {"tick": tick, "angle": f"{angle:.17g}"}
        for tick, angle in enumerate(rotated_angles)
    ]

    rotated_trajectory = []
    for tick in range(1, period + 1):
        absolute = phase + tick
        wraps, index = divmod(absolute, period)
        rotated_trajectory.append(
            {
                "tick": tick,
                "angle": f"{rotated_angles[tick - 1]:.17g}",
                "x": f"{wraps * x[period] + x[index] - x[phase]:.17g}",
                "y": f"{wraps * y[period] + y[index] - y[phase]:.17g}",
                "vx": f"{vx[index]:.17g}",
                "vy": f"{vy[index]:.17g}",
            }
        )

    write_rows(result_dir / "waveform.csv", rotated_waveform)
    write_rows(result_dir / "trajectory.csv", rotated_trajectory)
    rotation = {
        "highestPointAtTickZero": True,
        "rotationTicks": phase,
        "highestStateIndexBeforeRotation": highest,
    }
    phase_path.write_text(
        json.dumps(rotation, indent=2) + "\n", encoding="utf-8"
    )
    return phase


def main() -> None:
    for name in TARGETS:
        phase = rotate_result(name)
        print(f"{name}: rotated left by {phase} tick")


if __name__ == "__main__":
    main()
