from __future__ import annotations

import csv
import json
import math
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results"
STRATEGIES = ROOT / "strategies"


CONFIGS = {
    "periodic-vx025-no-drop": {
        "mirror": "periodic-vx025-no-drop",
        "name": "minimum_start_height_with_initial_speed",
        "description": "Minimum steady-state height span with nonnegative cycle height, interpreted as minimum start height for the optimized initial velocity.",
        "target": "min_height_span_with_dy_ge_0",
        "variant": "smooth optimum",
        "split": 100,
        "method": "Constrained steady-state height-span optimization with Java-exact coordinated refinement",
        "no_chatter": True,
    },
    "fastest-horizontal-speed": {
        "mirror": "fastest-horizontal-speed",
        "name": "fastest_horizontal_jitter_java_exact",
        "description": "Maximum steady-state horizontal speed with nonnegative cycle height; unrestricted per-tick jitter is allowed.",
        "target": "max_horizontal_speed_with_dy_ge_0",
        "variant": "optimum (per-frame jitter allowed)",
        "split": 271,
        "method": "Java-exact quantization-aware MILP coordinated frame moves",
        "no_chatter": False,
    },
    "fastest-horizontal-speed-smooth": {
        "mirror": "fastest-horizontal-speed-smooth",
        "name": "fastest_horizontal_no_chatter_java_exact",
        "description": "No-chatter counterpart of the fastest steady-state horizontal strategy, preserving phase jumps while removing rapid back-and-forth pitch reversals.",
        "target": "max_horizontal_speed_with_dy_ge_0_and_no_chatter",
        "variant": "no-chatter practical",
        "split": 271,
        "method": "TV trend filtering (weight 23), low-frequency cubic correction, and a monotone local bridge",
        "no_chatter": True,
    },
    "lbfgsb-max-climb-raw": {
        "mirror": "fastest-climb-rate-jitter",
        "name": "max_climb_jitter_java_exact",
        "description": "Maximum steady-state climb-rate reference with unrestricted per-tick jitter, validated with the Java-exact trigonometric lookup model.",
        "target": "max_climb_rate",
        "variant": "optimum (per-frame jitter allowed)",
        "split": 165,
        "method": "Per-frame optimization followed by Java-exact coordinate and endpoint refinement",
        "no_chatter": False,
    },
    "fastest-climb-rate": {
        "mirror": "fastest-climb-rate",
        "name": "max_climb_no_chatter_java_exact",
        "description": "Unrestricted per-frame no-chatter counterpart of the maximum steady-state climb strategy, retaining real phase jumps without rapid pitch alternation.",
        "target": "max_climb_rate_without_chatter",
        "variant": "no-chatter practical",
        "split": -1,
        "method": "Per-frame L-BFGS-B with reversal regularization, a four-direction-change topology limit, and Java-exact coordinate refinement",
        "no_chatter": True,
    },
}


def read_rows(path: Path) -> list[dict[str, float]]:
    with path.open(newline="", encoding="utf-8-sig") as file:
        return [
            {key: float(value) for key, value in row.items()}
            for row in csv.DictReader(file)
        ]


def write_params(path: Path, values: dict[str, object]) -> None:
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(("field", "value"))
        writer.writerows(values.items())


def build_metadata(result_name: str, config: dict[str, object]) -> tuple[dict, dict]:
    result_dir = RESULTS / result_name
    phase_path = result_dir / "phase.json"
    phase = json.loads(phase_path.read_text(encoding="utf-8")) if phase_path.exists() else {}
    waveform = read_rows(result_dir / "waveform.csv")
    trajectory = read_rows(result_dir / "trajectory.csv")
    angles = [row["angle"] for row in waveform]
    period = len(angles)
    delta = [angles[(index + 1) % period] - angles[index] for index in range(period)]
    ys = [0.0] + [row["y"] for row in trajectory]
    dx = trajectory[-1]["x"]
    dy = trajectory[-1]["y"]
    seconds = period / 20.0
    metrics = {
        "avgHorizontalBlocksPerSecond": dx / seconds,
        "climbRateBlocksPerSecond": dy / seconds,
        "dxBlocksPerCycle": dx,
        "dyBlocksPerCycle": dy,
        "heightSpanBlocks": max(ys) - min(ys),
        "minRelativeYBlocks": min(ys),
        "maxRelativeYBlocks": max(ys),
        "meanAbsDeltaDegreesPerTick": sum(abs(value) for value in delta) / period,
        "rmsDeltaDegreesPerTick": math.sqrt(sum(value * value for value in delta) / period),
        "maxAbsDeltaDegreesPerTick": max(abs(value) for value in delta),
        "minAngleDegrees": min(angles),
        "maxAngleDegrees": max(angles),
        "converged": True,
    }
    metadata = {
        "format": "elytra-framewise-periodic-v2",
        "name": config["name"],
        "description": config["description"],
        "variant": config["variant"],
        "tickRate": 20,
        "periodTicks": period,
        "optimizerSplitTickBeforePhaseRotation": config["split"],
        "phaseOrigin": "highest steady-state point",
        "phaseRotationTicks": int(phase.get("rotationTicks", 0)),
        "angleUnit": "degrees",
        "angleConvention": "positive angle means nose-up; negative angle means nose-down",
        "minecraftPitchConvention": "minecraft_pitch_degrees = -angle_degrees",
        "optimizationTarget": config["target"],
        "javaExactValidation": True,
        "shortPeriodChatterRemoved": config["no_chatter"],
        "method": config["method"],
        "files": {"waveform": "waveform.csv", "trajectory": "trajectory.csv"},
        "steadyStateMetrics": metrics,
    }
    if config.get("preserve_best_params"):
        metadata["controlParameterFile"] = "best_params.csv"
    params = {
        "format": metadata["format"],
        "variant": config["variant"],
        "optimizationTarget": config["target"],
        "period": period,
        "optimizerSplitBeforePhaseRotation": config["split"],
        "phaseRotationTicks": int(phase.get("rotationTicks", 0)),
        "avgHorizontal": metrics["avgHorizontalBlocksPerSecond"],
        "climbRate": metrics["climbRateBlocksPerSecond"],
        "dx": dx,
        "dy": dy,
        "heightSpan": metrics["heightSpanBlocks"],
        "meanAbsDelta": metrics["meanAbsDeltaDegreesPerTick"],
        "rmsDelta": metrics["rmsDeltaDegreesPerTick"],
        "maxAbsDelta": metrics["maxAbsDeltaDegreesPerTick"],
        "noShortPeriodChatter": int(bool(config["no_chatter"])),
        "method": config["method"],
    }
    return metadata, params


def main() -> None:
    for result_name, config in CONFIGS.items():
        result_dir = RESULTS / result_name
        mirror_dir = STRATEGIES / str(config["mirror"])
        mirror_dir.mkdir(parents=True, exist_ok=True)
        metadata, params = build_metadata(result_name, config)
        summary = {
            "name": metadata["name"],
            "periodTicks": metadata["periodTicks"],
            "phaseOrigin": metadata["phaseOrigin"],
            "phaseRotationTicks": metadata["phaseRotationTicks"],
            **metadata["steadyStateMetrics"],
        }
        with (result_dir / "summary.json").open("w", encoding="utf-8") as file:
            json.dump(summary, file, ensure_ascii=False, indent=2)
            file.write("\n")
        for path in (result_dir / "strategy.json", mirror_dir / "parameters.json"):
            with path.open("w", encoding="utf-8") as file:
                json.dump(metadata, file, ensure_ascii=False, indent=2)
                file.write("\n")
        if config.get("preserve_best_params"):
            shutil.copyfile(result_dir / "best_params.csv", mirror_dir / "best_params.csv")
        else:
            for path in (result_dir / "best_params.csv", mirror_dir / "best_params.csv"):
                write_params(path, params)
        shutil.copyfile(result_dir / "waveform.csv", mirror_dir / "waveform.csv")
        print(f"refreshed {result_name} -> strategies/{config['mirror']}")


if __name__ == "__main__":
    main()
