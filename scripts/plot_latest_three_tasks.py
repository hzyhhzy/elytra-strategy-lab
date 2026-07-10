from __future__ import annotations

import csv
import math
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib import font_manager


ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results"
IMAGES = ROOT / "docs" / "images"


PLOTS = (
    (
        "periodic-vx025-no-drop",
        "periodic-vx025-no-drop",
        "有初速度的最小起步高度",
        "周期 162 tick，高度起伏 25.560603 格，dy +4.31e-8 格",
        "Minimum Start Height With Initial Speed",
        "period 162 tick, height span 25.560603 blocks, dy +4.31e-8 blocks",
    ),
    (
        "lbfgsb-max-climb-raw",
        "lbfgsb-max-climb-raw",
        "稳态最大平均升速最优解（不禁止逐帧抖动）",
        "周期 255 tick，平均升速 1.561550761 格/秒，dy +19.909772 格",
        "Maximum Steady-State Climb Optimum (Per-Frame Jitter Allowed)",
        "period 255 tick, climb 1.561550761 blocks/s, dy +19.909772 blocks",
    ),
    (
        "fastest-climb-rate",
        "fastest-climb-rate",
        "稳态最大平均升速（无来回抖动）",
        "周期 254 tick，平均升速 1.552981247 格/秒，dy +19.722862 格",
        "Maximum Steady-State Climb (No Chatter)",
        "period 254 tick, climb 1.552981247 blocks/s, dy +19.722862 blocks",
    ),
    (
        "fastest-horizontal-speed",
        "fastest-horizontal-speed",
        "不掉高稳态最快水平速度最优解（不禁止逐帧抖动）",
        "周期 357 tick，平均水平速度 33.022449116 格/秒，dy +5.90e-8 格",
        "Fastest Non-Dropping Horizontal Flight Optimum (Per-Frame Jitter Allowed)",
        "period 357 tick, horizontal 33.022449116 blocks/s, dy +5.90e-8 blocks",
    ),
    (
        "fastest-horizontal-speed-smooth",
        "fastest-horizontal-speed-smooth",
        "不掉高稳态最快水平速度（无来回抖动）",
        "周期 357 tick，平均水平速度 33.011007670 格/秒，dy +0.000630 格",
        "Fastest Non-Dropping Horizontal Flight (No Chatter)",
        "period 357 tick, horizontal 33.011007670 blocks/s, dy +0.000630 blocks",
    ),
)


LABELS = {
    "zh": {
        "pitch": "仰角时序曲线",
        "pitch_y": "仰角（度）",
        "horizontal": "水平速度曲线",
        "horizontal_y": "水平速度（格/秒）",
        "trajectory": "轨迹曲线",
        "trajectory_x": "水平位移（格）",
        "trajectory_y": "高度变化（格）",
        "vertical": "垂直速度曲线",
        "vertical_y": "垂直速度（格/秒）",
    },
    "en": {
        "pitch": "Pitch Timeline",
        "pitch_y": "Pitch (degrees)",
        "horizontal": "Horizontal Speed",
        "horizontal_y": "Horizontal speed (blocks/s)",
        "trajectory": "Trajectory",
        "trajectory_x": "Horizontal distance (blocks)",
        "trajectory_y": "Height change (blocks)",
        "vertical": "Vertical Speed",
        "vertical_y": "Vertical speed (blocks/s)",
    },
}


def configure_font() -> None:
    available = {font.name for font in font_manager.fontManager.ttflist}
    for name in ("Microsoft YaHei", "SimHei", "SimSun", "Noto Sans CJK SC"):
        if name in available:
            plt.rcParams["font.sans-serif"] = [name]
            break
    plt.rcParams["axes.unicode_minus"] = False


def read_rows(path: Path) -> list[dict[str, float]]:
    with path.open(newline="", encoding="utf-8-sig") as file:
        return [
            {key: float(value) for key, value in row.items()}
            for row in csv.DictReader(file)
        ]


def limits(values: list[float]) -> tuple[float, float]:
    low = min(values)
    high = max(values)
    if math.isclose(low, high):
        return low - 1.0, high + 1.0
    padding = 0.06 * (high - low)
    return low - padding, high + padding


def draw(result: str, output: str, title: str, summary: str, language: str) -> None:
    waveform = read_rows(RESULTS / result / "waveform.csv")
    trajectory = read_rows(RESULTS / result / "trajectory.csv")
    labels = LABELS[language]
    angle_ticks = [row["tick"] for row in waveform]
    angles = [row["angle"] for row in waveform]
    ticks = [row["tick"] for row in trajectory]
    xs = [row["x"] for row in trajectory]
    ys = [row["y"] for row in trajectory]
    vx = [20.0 * row["vx"] for row in trajectory]
    vy = [20.0 * row["vy"] for row in trajectory]

    fig, axes = plt.subplots(2, 2, figsize=(14, 9), dpi=180)
    fig.suptitle(f"{title}\n{summary}", fontsize=15, y=0.985)

    axes[0, 0].plot(angle_ticks, angles, color="#007e86", linewidth=1.25)
    axes[0, 0].set_title(labels["pitch"])
    axes[0, 0].set_xlabel("tick")
    axes[0, 0].set_ylabel(labels["pitch_y"])
    axes[0, 0].set_xlim(0, max(angle_ticks))
    axes[0, 0].set_ylim(-95, 95)

    axes[0, 1].plot(ticks, vx, color="#2457a7", linewidth=1.3)
    axes[0, 1].set_title(labels["horizontal"])
    axes[0, 1].set_xlabel("tick")
    axes[0, 1].set_ylabel(labels["horizontal_y"])
    axes[0, 1].set_xlim(min(ticks), max(ticks))
    axes[0, 1].set_ylim(*limits(vx))

    axes[1, 0].plot(xs, ys, color="#d56a1d", linewidth=1.5)
    axes[1, 0].axhline(0.0, color="#10212a", linewidth=0.8, alpha=0.35)
    axes[1, 0].set_title(labels["trajectory"])
    axes[1, 0].set_xlabel(labels["trajectory_x"])
    axes[1, 0].set_ylabel(labels["trajectory_y"])
    axes[1, 0].set_xlim(*limits(xs))
    axes[1, 0].set_ylim(*limits(ys))

    axes[1, 1].plot(ticks, vy, color="#b42318", linewidth=1.3)
    axes[1, 1].axhline(0.0, color="#10212a", linewidth=0.8, alpha=0.35)
    axes[1, 1].set_title(labels["vertical"])
    axes[1, 1].set_xlabel("tick")
    axes[1, 1].set_ylabel(labels["vertical_y"])
    axes[1, 1].set_xlim(min(ticks), max(ticks))
    axes[1, 1].set_ylim(*limits(vy))

    for axis in axes.flat:
        axis.grid(True, alpha=0.23)
        axis.tick_params(labelsize=9)
    fig.tight_layout(rect=(0, 0, 1, 0.93))
    IMAGES.mkdir(parents=True, exist_ok=True)
    fig.savefig(IMAGES / output, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    configure_font()
    for result, base, zh_title, zh_summary, en_title, en_summary in PLOTS:
        draw(result, f"{base}.png", zh_title, zh_summary, "zh")
        draw(result, f"{base}-en.png", en_title, en_summary, "en")
        print(f"generated {base}.png and {base}-en.png")


if __name__ == "__main__":
    main()
