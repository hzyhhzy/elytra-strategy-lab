from __future__ import annotations

import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "simulator" / "strategies-data.js"
STRATEGIES = (
    "from-rest-gain-two",
    "from-rest-return-height",
    "fastest-climb-rate",
    "fastest-horizontal-speed",
)


def read_angles(name: str) -> list[float]:
    path = ROOT / "strategies" / name / "waveform.csv"
    with path.open(newline="", encoding="utf-8-sig") as file:
        return [float(row["angle"]) for row in csv.DictReader(file)]


def main() -> None:
    lines = [
        "// Generated from elytra-strategy-lab/strategies/*/waveform.csv so index.html can run from file://.",
        "window.ELYTRA_STRATEGY_WAVEFORMS = Object.freeze({",
    ]
    for name in STRATEGIES:
        lines.append(f'  "{name}": Object.freeze([')
        lines.extend(f"    {angle:.17g}," for angle in read_angles(name))
        lines.append("  ]),")
    lines.extend(("});", ""))
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    print(OUTPUT)


if __name__ == "__main__":
    main()
