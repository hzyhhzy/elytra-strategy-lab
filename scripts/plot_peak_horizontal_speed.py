from __future__ import annotations

import csv
import json
import math
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib import font_manager


ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "results"
IMAGES = ROOT / "docs" / "images"
INITIAL_VY = -3.920003814700875


CASES = (
    {
        "directory": RESULTS / "peak-horizontal-speed",
        "stem": "peak-horizontal-speed",
        "title_en": "Maximum Instantaneous Horizontal Speed",
        "title_zh": "\u6700\u5927\u77ac\u65f6\u6c34\u5e73\u901f\u5ea6",
        "line_width": 0.85,
    },
    {
        "directory": RESULTS / "peak-horizontal-speed-smooth",
        "stem": "peak-horizontal-speed-smooth",
        "title_en": "Maximum Instantaneous Horizontal Speed: Smooth Alternative",
        "title_zh": "\u6700\u5927\u77ac\u65f6\u6c34\u5e73\u901f\u5ea6\uff1a\u5e73\u6ed1\u66ff\u4ee3",
        "line_width": 1.5,
    },
)


def configure_font() -> None:
    available = {font.name for font in font_manager.fontManager.ttflist}
    for name in ("Microsoft YaHei", "SimHei", "SimSun", "Noto Sans CJK SC"):
        if name in available:
            plt.rcParams["font.sans-serif"] = [name]
            break
    plt.rcParams["axes.unicode_minus"] = False


def read_rows(path: Path) -> list[dict[str, float]]:
    with path.open(newline="", encoding="utf-8") as file:
        return [
            {key: float(value) for key, value in row.items()}
            for row in csv.DictReader(file)
        ]


def padded(values: np.ndarray, ratio: float = 0.06) -> tuple[float, float]:
    low = float(np.min(values))
    high = float(np.max(values))
    if math.isclose(low, high):
        return low - 1.0, high + 1.0
    margin = ratio * (high - low)
    return low - margin, high + margin


def draw(case: dict, language: str) -> Path:
    rows = read_rows(case["directory"] / "trajectory.csv")
    summary = json.loads(
        (case["directory"] / "summary.json").read_text(encoding="utf-8")
    )

    tick = np.array([row["tick"] for row in rows])
    angle = np.array([row["angle"] for row in rows])
    x = np.r_[0.0, [row["x"] for row in rows]]
    y = np.r_[0.0, [row["y"] for row in rows]]
    vx = np.array([row["horizontal_bps"] for row in rows])
    vy = np.array([row["vertical_bps"] for row in rows])

    if language == "zh":
        title = case["title_zh"]
        subtitle = (
            f"\u521d\u901f\u5ea6 (0, {INITIAL_VY:.6f}) \u683c/tick\uff1b"
            f"\u5cf0\u503c {summary['best_horizontal_bps']:.6f} \u683c/\u79d2\uff0c"
            f"tick {summary['best_tick']}\uff1b\u4e0b\u964d {-summary['final_drop_m']:.3f} \u683c"
        )
        labels = (
            "\u4ef0\u89d2\u65f6\u5e8f\u66f2\u7ebf",
            "\u4ef0\u89d2\uff08\u5ea6\uff09",
            "\u6c34\u5e73\u901f\u5ea6\u66f2\u7ebf",
            "\u6c34\u5e73\u901f\u5ea6\uff08\u683c/\u79d2\uff09",
            "\u8f68\u8ff9\u66f2\u7ebf",
            "\u6c34\u5e73\u4f4d\u79fb\uff08\u683c\uff09",
            "\u9ad8\u5ea6\u53d8\u5316\uff08\u683c\uff09",
            "\u5782\u76f4\u901f\u5ea6\u66f2\u7ebf",
            "\u5782\u76f4\u901f\u5ea6\uff08\u683c/\u79d2\uff09",
        )
        output = IMAGES / f"{case['stem']}.png"
    else:
        title = case["title_en"]
        subtitle = (
            f"Initial velocity (0, {INITIAL_VY:.6f}) blocks/tick; "
            f"peak {summary['best_horizontal_bps']:.6f} blocks/s at tick "
            f"{summary['best_tick']}; drop {-summary['final_drop_m']:.3f} blocks"
        )
        labels = (
            "Pitch Timeline",
            "Pitch (degrees)",
            "Horizontal Speed",
            "Horizontal speed (blocks/s)",
            "Trajectory",
            "Horizontal distance (blocks)",
            "Height change (blocks)",
            "Vertical Speed",
            "Vertical speed (blocks/s)",
        )
        output = IMAGES / f"{case['stem']}-en.png"

    fig, axes = plt.subplots(2, 2, figsize=(14, 9), dpi=180)
    fig.suptitle(f"{title}\n{subtitle}", fontsize=15, y=0.985)

    axes[0, 0].plot(tick, angle, color="#007e86", linewidth=case["line_width"])
    axes[0, 0].set_title(labels[0])
    axes[0, 0].set_xlabel("tick")
    axes[0, 0].set_ylabel(labels[1])
    axes[0, 0].set_xlim(1, len(tick))
    axes[0, 0].set_ylim(-95, 5)

    axes[0, 1].plot(tick, vx, color="#2457a7", linewidth=1.4)
    axes[0, 1].axvline(summary["best_tick"], color="#b42318", linewidth=0.9, alpha=0.55)
    axes[0, 1].set_title(labels[2])
    axes[0, 1].set_xlabel("tick")
    axes[0, 1].set_ylabel(labels[3])
    axes[0, 1].set_xlim(1, len(tick))
    axes[0, 1].set_ylim(*padded(vx))

    axes[1, 0].plot(x, y, color="#d56a1d", linewidth=1.5)
    axes[1, 0].scatter([x[-1]], [y[-1]], color="#b42318", s=28, zorder=3)
    axes[1, 0].set_title(labels[4])
    axes[1, 0].set_xlabel(labels[5])
    axes[1, 0].set_ylabel(labels[6])
    axes[1, 0].set_xlim(*padded(x))
    axes[1, 0].set_ylim(*padded(y))

    axes[1, 1].plot(tick, vy, color="#b42318", linewidth=1.4)
    axes[1, 1].axhline(0.0, color="#10212a", linewidth=0.8, alpha=0.35)
    axes[1, 1].set_title(labels[7])
    axes[1, 1].set_xlabel("tick")
    axes[1, 1].set_ylabel(labels[8])
    axes[1, 1].set_xlim(1, len(tick))
    axes[1, 1].set_ylim(*padded(vy))

    for axis in axes.flat:
        axis.grid(True, alpha=0.23)
        axis.tick_params(labelsize=9)
    fig.tight_layout(rect=(0, 0, 1, 0.92))
    fig.savefig(output, bbox_inches="tight")
    plt.close(fig)
    return output


def main() -> None:
    configure_font()
    IMAGES.mkdir(parents=True, exist_ok=True)
    for case in CASES:
        print(draw(case, "zh"))
        print(draw(case, "en"))


if __name__ == "__main__":
    main()
