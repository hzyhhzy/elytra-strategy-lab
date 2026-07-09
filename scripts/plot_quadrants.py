from pathlib import Path
import csv
import math

import matplotlib.pyplot as plt
from matplotlib import font_manager


ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results"
IMAGES = ROOT / "docs" / "images"
TICKS_PER_SECOND = 20.0


def configure_font():
    candidates = [
        "Microsoft YaHei",
        "SimHei",
        "SimSun",
        "Noto Sans CJK SC",
        "Source Han Sans SC",
        "Arial Unicode MS",
    ]
    available = {font.name for font in font_manager.fontManager.ttflist}
    for name in candidates:
        if name in available:
            plt.rcParams["font.sans-serif"] = [name]
            break
    plt.rcParams["axes.unicode_minus"] = False


def read_csv(path):
    with path.open("r", newline="", encoding="utf-8") as f:
        return [
            {key: float(value) if key != "tick" else int(value) for key, value in row.items()}
            for row in csv.DictReader(f)
        ]


def limits(values, padding=0.06):
    low = min(values)
    high = max(values)
    if math.isclose(low, high):
        return low - 1.0, high + 1.0
    span = high - low
    return low - span * padding, high + span * padding


LABELS_ZH = {
    "title_open": "（",
    "title_close": "）",
    "pitch_title": "仰角时序曲线",
    "pitch_ylabel": "仰角（度）",
    "vx_title": "水平速度曲线",
    "vx_ylabel": "水平速度（方块/秒）",
    "trajectory_title": "轨迹曲线",
    "trajectory_xlabel": "水平位移（方块）",
    "trajectory_ylabel": "高度变化（方块）",
    "vy_title": "垂直速度曲线",
    "vy_ylabel": "垂直速度（方块/秒）",
}

LABELS_EN = {
    "title_open": " (",
    "title_close": ")",
    "pitch_title": "Pitch Timeline",
    "pitch_ylabel": "Pitch (degrees)",
    "vx_title": "Horizontal Speed",
    "vx_ylabel": "Horizontal speed (blocks/s)",
    "trajectory_title": "Trajectory",
    "trajectory_xlabel": "Horizontal distance (blocks)",
    "trajectory_ylabel": "Height change (blocks)",
    "vy_title": "Vertical Speed",
    "vy_ylabel": "Vertical speed (blocks/s)",
}


def draw(result_dir, image_name, title, summary, labels):
    waveform = read_csv(RESULTS / result_dir / "waveform.csv")
    trajectory = read_csv(RESULTS / result_dir / "trajectory.csv")

    ticks = [row["tick"] for row in trajectory]
    angle_ticks = [row["tick"] for row in waveform]
    angles = [row["angle"] for row in waveform]
    xs = [row["x"] for row in trajectory]
    ys = [row["y"] for row in trajectory]
    vx = [row["vx"] * TICKS_PER_SECOND for row in trajectory]
    vy = [row["vy"] * TICKS_PER_SECOND for row in trajectory]

    fig, axes = plt.subplots(2, 2, figsize=(14, 9), dpi=160)
    fig.suptitle(f"{title}{labels['title_open']}{summary}{labels['title_close']}", fontsize=17, y=0.98)

    ax = axes[0, 0]
    ax.plot(angle_ticks, angles, color="#007e86", linewidth=1.8)
    ax.set_title(labels["pitch_title"])
    ax.set_xlabel("tick")
    ax.set_ylabel(labels["pitch_ylabel"])
    ax.set_xlim(0, max(angle_ticks))
    ax.set_ylim(-95, 95)
    ax.grid(True, alpha=0.25)

    ax = axes[0, 1]
    ax.plot(ticks, vx, color="#2457a7", linewidth=1.7)
    ax.set_title(labels["vx_title"])
    ax.set_xlabel("tick")
    ax.set_ylabel(labels["vx_ylabel"])
    ax.set_xlim(0, max(ticks))
    ax.set_ylim(*limits(vx))
    ax.grid(True, alpha=0.25)

    ax = axes[1, 0]
    ax.plot(xs, ys, color="#d56a1d", linewidth=1.9)
    ax.set_title(labels["trajectory_title"])
    ax.set_xlabel(labels["trajectory_xlabel"])
    ax.set_ylabel(labels["trajectory_ylabel"])
    ax.set_xlim(*limits(xs))
    ax.set_ylim(*limits(ys))
    ax.grid(True, alpha=0.25)

    ax = axes[1, 1]
    ax.plot(ticks, vy, color="#b42318", linewidth=1.7)
    ax.axhline(0, color="#10212a", linewidth=0.8, alpha=0.35)
    ax.set_title(labels["vy_title"])
    ax.set_xlabel("tick")
    ax.set_ylabel(labels["vy_ylabel"])
    ax.set_xlim(0, max(ticks))
    ax.set_ylim(*limits(vy))
    ax.grid(True, alpha=0.25)

    fig.tight_layout(rect=(0, 0, 1, 0.95))
    IMAGES.mkdir(parents=True, exist_ok=True)
    fig.savefig(IMAGES / image_name, bbox_inches="tight")
    plt.close(fig)


def main():
    configure_font()
    plots = [
        (
            "from-rest-gain-two",
            "from-rest-gain-two",
            "初始 0 速度，至少提升 2 格",
            "最低需要初始高度 35.122 方块，达到 tick 217",
            "From Rest, Gain At Least 2 Blocks",
            "minimum start height 35.122 blocks, reaches tick 217",
        ),
        (
            "from-rest-return-height",
            "from-rest-return-height",
            "初始 0 速度，高度至少不变",
            "最低需要初始高度 32.348 方块，返回 tick 208",
            "From Rest, Return To Start Height",
            "minimum start height 32.348 blocks, returns at tick 208",
        ),
        (
            "fastest-climb-rate",
            "fastest-climb-rate",
            "最快升速策略",
            "周期 254 tick，平均升速 1.547 方块/秒",
            "Fastest Climb Strategy",
            "period 254 tick, average climb 1.547 blocks/s",
        ),
        (
            "fastest-horizontal-speed",
            "fastest-horizontal-speed",
            "最快水平速度策略",
            "周期 357 tick，平均水平速度 32.993 方块/秒",
            "Fastest Horizontal Strategy",
            "period 357 tick, average horizontal speed 32.993 blocks/s",
        ),
        (
            "periodic-vx025-no-drop",
            "periodic-vx025-no-drop",
            "有初速度，高度不下降",
            "周期 170 tick，落差 25.803 方块，dy +0.000138",
            "Initial-Speed No-Drop Strategy",
            "period 170 tick, height span 25.803 blocks, dy +0.000138",
        ),
        (
            "periodic-gain-one",
            "periodic-gain-one",
            "周期高度 +1 策略",
            "周期 179 tick，落差 28.523 方块，dy +1.003",
            "Periodic Height +1 Strategy",
            "period 179 tick, height span 28.523 blocks, dy +1.003",
        ),
    ]
    for result_dir, base_name, title_zh, summary_zh, title_en, summary_en in plots:
        draw(result_dir, f"{base_name}.png", title_zh, summary_zh, LABELS_ZH)
        draw(result_dir, f"{base_name}-en.png", title_en, summary_en, LABELS_EN)


if __name__ == "__main__":
    main()
