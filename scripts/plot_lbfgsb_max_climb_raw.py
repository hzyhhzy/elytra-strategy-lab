from __future__ import annotations

import csv
import json
import math
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib import font_manager


ROOT = Path(__file__).resolve().parents[1]
RESULT_DIR = ROOT / "results" / "lbfgsb-max-climb-raw"
IMAGE_DIR = ROOT / "docs" / "images"
TICKS_PER_SECOND = 20.0


def configure_font() -> None:
    available = {font.name for font in font_manager.fontManager.ttflist}
    for name in ["Microsoft YaHei", "SimHei", "SimSun", "Noto Sans CJK SC", "Source Han Sans SC", "Arial Unicode MS"]:
        if name in available:
            plt.rcParams["font.sans-serif"] = [name]
            break
    plt.rcParams["axes.unicode_minus"] = False


def read_csv(path: Path) -> list[dict[str, float]]:
    with path.open(newline="", encoding="utf-8-sig") as f:
        return [{key: float(value) for key, value in row.items()} for row in csv.DictReader(f)]


def limits(values: np.ndarray, padding: float = 0.06) -> tuple[float, float]:
    low = float(np.min(values))
    high = float(np.max(values))
    if math.isclose(low, high):
        return low - 1.0, high + 1.0
    span = high - low
    return low - span * padding, high + span * padding


LABELS = {
    "zh": {
        "title": "逐帧 L-BFGS-B 最大升速原始解",
        "summary": "平均升速 {climb:.9f} 方块/秒，dy {dy:+.6f}，有剧烈抖动",
        "pitch_title": "仰角时序曲线",
        "pitch_ylabel": "仰角（度）",
        "vx_title": "水平速度曲线",
        "vx_ylabel": "方块/秒",
        "trajectory_title": "轨迹曲线",
        "trajectory_xlabel": "水平位移（方块）",
        "trajectory_ylabel": "高度变化（方块）",
        "vy_title": "垂直速度曲线",
        "vy_ylabel": "方块/秒",
    },
    "en": {
        "title": "Raw Per-Frame L-BFGS-B Max-Climb Solution",
        "summary": "average climb {climb:.9f} blocks/s, dy {dy:+.6f}, severe pitch jitter",
        "pitch_title": "Pitch Timeline",
        "pitch_ylabel": "Pitch (degrees)",
        "vx_title": "Horizontal Speed",
        "vx_ylabel": "blocks/s",
        "trajectory_title": "Trajectory",
        "trajectory_xlabel": "Horizontal distance (blocks)",
        "trajectory_ylabel": "Height change (blocks)",
        "vy_title": "Vertical Speed",
        "vy_ylabel": "blocks/s",
    },
}


def draw(language: str, output_name: str) -> None:
    labels = LABELS[language]
    trajectory = read_csv(RESULT_DIR / "trajectory.csv")
    with (RESULT_DIR / "strategy.json").open(encoding="utf-8") as f:
        strategy = json.load(f)
    metrics = strategy["steadyStateMetrics"]

    ticks = np.array([row["tick"] for row in trajectory], dtype=float)
    angle = np.array([row["angle"] for row in trajectory], dtype=float)
    x = np.array([row["x"] for row in trajectory], dtype=float)
    y = np.array([row["y"] for row in trajectory], dtype=float)
    vx = np.array([row["vx"] for row in trajectory], dtype=float) * TICKS_PER_SECOND
    vy = np.array([row["vy"] for row in trajectory], dtype=float) * TICKS_PER_SECOND

    fig, axes = plt.subplots(2, 2, figsize=(14, 9), dpi=170)
    fig.suptitle(
        f"{labels['title']}: "
        + labels["summary"].format(
            climb=metrics["climbRateBlocksPerSecond"],
            dy=metrics["dyBlocksPerCycle"],
        ),
        fontsize=16,
        y=0.98,
    )

    ax = axes[0, 0]
    ax.plot(ticks, angle, color="#007e86", linewidth=1.45)
    ax.set_title(labels["pitch_title"])
    ax.set_xlabel("tick")
    ax.set_ylabel(labels["pitch_ylabel"])
    ax.set_xlim(0, float(np.max(ticks)))
    ax.set_ylim(-95, 95)
    ax.grid(True, alpha=0.25)

    ax = axes[0, 1]
    ax.plot(ticks, vx, color="#2457a7", linewidth=1.45)
    ax.set_title(labels["vx_title"])
    ax.set_xlabel("tick")
    ax.set_ylabel(labels["vx_ylabel"])
    ax.set_xlim(0, float(np.max(ticks)))
    ax.set_ylim(*limits(vx))
    ax.grid(True, alpha=0.25)

    ax = axes[1, 0]
    ax.plot(x, y, color="#d56a1d", linewidth=1.7)
    ax.set_title(labels["trajectory_title"])
    ax.set_xlabel(labels["trajectory_xlabel"])
    ax.set_ylabel(labels["trajectory_ylabel"])
    ax.set_xlim(*limits(x))
    ax.set_ylim(*limits(y))
    ax.grid(True, alpha=0.25)

    ax = axes[1, 1]
    ax.plot(ticks, vy, color="#b42318", linewidth=1.45)
    ax.axhline(0, color="#10212a", linewidth=0.8, alpha=0.35)
    ax.set_title(labels["vy_title"])
    ax.set_xlabel("tick")
    ax.set_ylabel(labels["vy_ylabel"])
    ax.set_xlim(0, float(np.max(ticks)))
    ax.set_ylim(*limits(vy))
    ax.grid(True, alpha=0.25)

    fig.tight_layout(rect=(0, 0, 1, 0.95))
    IMAGE_DIR.mkdir(parents=True, exist_ok=True)
    fig.savefig(IMAGE_DIR / output_name, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    configure_font()
    draw("zh", "lbfgsb-max-climb-raw.png")
    draw("en", "lbfgsb-max-climb-raw-en.png")


if __name__ == "__main__":
    main()
