#!/usr/bin/env python3
"""Render MetaModule faceplates at their exact native pixel dimensions."""

from pathlib import Path

import cairosvg


ROOT = Path(__file__).resolve().parents[1]
PANELS = {
    "Program": (304, 240),
    "TimingMonitor": (114, 240),
    "Head": (152, 240),
    "Stage4": (114, 240),
    "Midi": (114, 240),
    "ProbeCore": (76, 240),
    "ProbeRemote": (76, 240),
}


def main() -> None:
    for name, (width, height) in PANELS.items():
        source = ROOT / "artwork" / f"{name}.svg"
        target = ROOT / "assets" / f"{name}.png"
        cairosvg.svg2png(
            url=str(source),
            write_to=str(target),
            output_width=width,
            output_height=height,
        )
        print(f"{name}: {width}x{height}")


if __name__ == "__main__":
    main()
