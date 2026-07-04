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


def draw(result_dir, image_name, title, summary):
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
    fig.suptitle(f"{title}（{summary}）", fontsize=17, y=0.98)

    ax = axes[0, 0]
    ax.plot(angle_ticks, angles, color="#007e86", linewidth=1.8)
    ax.set_title("仰角时序曲线")
    ax.set_xlabel("tick")
    ax.set_ylabel("仰角（度）")
    ax.set_xlim(0, max(angle_ticks))
    ax.set_ylim(-95, 95)
    ax.grid(True, alpha=0.25)

    ax = axes[0, 1]
    ax.plot(ticks, vx, color="#2457a7", linewidth=1.7)
    ax.set_title("水平速度曲线")
    ax.set_xlabel("tick")
    ax.set_ylabel("水平速度（方块/秒）")
    ax.set_xlim(0, max(ticks))
    ax.set_ylim(*limits(vx))
    ax.grid(True, alpha=0.25)

    ax = axes[1, 0]
    ax.plot(xs, ys, color="#d56a1d", linewidth=1.9)
    ax.set_title("轨迹曲线")
    ax.set_xlabel("水平位移（方块）")
    ax.set_ylabel("高度变化（方块）")
    ax.set_xlim(*limits(xs))
    ax.set_ylim(*limits(ys))
    ax.grid(True, alpha=0.25)

    ax = axes[1, 1]
    ax.plot(ticks, vy, color="#b42318", linewidth=1.7)
    ax.axhline(0, color="#10212a", linewidth=0.8, alpha=0.35)
    ax.set_title("垂直速度曲线")
    ax.set_xlabel("tick")
    ax.set_ylabel("垂直速度（方块/秒）")
    ax.set_xlim(0, max(ticks))
    ax.set_ylim(*limits(vy))
    ax.grid(True, alpha=0.25)

    fig.tight_layout(rect=(0, 0, 1, 0.95))
    IMAGES.mkdir(parents=True, exist_ok=True)
    fig.savefig(IMAGES / image_name, bbox_inches="tight")
    plt.close(fig)


def main():
    configure_font()
    draw("from-rest-gain-two", "from-rest-gain-two.png", "初始 0 速度，至少提升 2 格", "最低需要初始高度 35.122 方块，达到 tick 217")
    draw("from-rest-return-height", "from-rest-return-height.png", "初始 0 速度，高度至少不变", "最低需要初始高度 32.348 方块，返回 tick 208")
    draw("fastest-climb-rate", "fastest-climb-rate.png", "最快升速策略", "周期 254 tick，平均升速 1.547 方块/秒")
    draw("fastest-horizontal-speed", "fastest-horizontal-speed.png", "最快水平速度策略", "周期 357 tick，平均水平速度 32.993 方块/秒")


if __name__ == "__main__":
    main()

