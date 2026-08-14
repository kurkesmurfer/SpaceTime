#!/usr/bin/env python3
"""Render MetaModule panels with bundled fonts at native resolution."""

from __future__ import annotations

import io
import xml.etree.ElementTree as ET
from pathlib import Path

import cairosvg
from PIL import Image, ImageColor, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = ROOT.parent / "vcv" / "res" / "fonts"
SUPERSAMPLE = 8
PANELS = {
    "Program": (304, 240),
    "TimingMonitor": (114, 240),
    "Head": (152, 240),
    "Stage4": (114, 240),
    "Midi": (114, 240),
    "ProbeCore": (76, 240),
    "ProbeRemote": (76, 240),
}
FONTS = {
    ("Fraunces", "600"): FONT_DIR / "Fraunces-SemiBold.ttf",
    ("Barlow Condensed", "500"): FONT_DIR / "BarlowCondensed-Medium.ttf",
    ("Barlow Condensed", "600"): FONT_DIR / "BarlowCondensed-SemiBold.ttf",
    ("Barlow Condensed", "700"): FONT_DIR / "BarlowCondensed-Bold.ttf",
}


def inherited(element: ET.Element, parents: dict[ET.Element, ET.Element], key: str, default: str) -> str:
    node = element
    while node is not None:
        if key in node.attrib:
            return node.attrib[key]
        node = parents.get(node)
    return default


def draw_spaced(draw: ImageDraw.ImageDraw, xy: tuple[float, float], text: str,
                font: ImageFont.FreeTypeFont, fill: str, spacing: float,
                anchor: str) -> None:
    widths = [draw.textlength(ch, font=font) for ch in text]
    total = sum(widths) + spacing * max(0, len(text) - 1)
    if anchor[0] == "m":
        x = xy[0] - total / 2
    elif anchor[0] == "r":
        x = xy[0] - total
    else:
        x = xy[0]
    vertical = anchor[1]
    for ch, width in zip(text, widths):
        draw.text((x, xy[1]), ch, font=font, fill=fill, anchor="l" + vertical)
        x += width + spacing


def render(name: str, target_size: tuple[int, int]) -> None:
    source = ROOT / "artwork" / f"{name}.svg"
    tree = ET.parse(source)
    root = tree.getroot()
    view = [float(value) for value in root.attrib["viewBox"].split()]
    mm_width, mm_height = view[2], view[3]
    parents = {child: parent for parent in tree.iter() for child in parent}
    labels: list[dict[str, str | float]] = []

    for element in list(tree.iter()):
        if element.tag.rsplit("}", 1)[-1] != "text":
            continue
        family = inherited(element, parents, "font-family", "Barlow Condensed")
        weight = inherited(element, parents, "font-weight", "600")
        anchor = inherited(element, parents, "text-anchor", "start")
        baseline = inherited(element, parents, "dominant-baseline", "alphabetic")
        labels.append({
            "text": "".join(element.itertext()),
            "x": float(element.attrib.get("x", "0")),
            "y": float(element.attrib.get("y", "0")),
            "family": family,
            "weight": weight,
            "size": float(inherited(element, parents, "font-size", "2.5")),
            "spacing": float(inherited(element, parents, "letter-spacing", "0")),
            "fill": inherited(element, parents, "fill", "#332d27"),
            "opacity": float(inherited(element, parents, "opacity", "1")),
            "anchor": anchor,
            "baseline": baseline,
        })
        parents[element].remove(element)

    large = (target_size[0] * SUPERSAMPLE, target_size[1] * SUPERSAMPLE)
    background = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    raw = cairosvg.svg2png(bytestring=background, url=str(source),
                          output_width=large[0], output_height=large[1])
    image = Image.open(io.BytesIO(raw)).convert("RGBA")
    draw = ImageDraw.Draw(image)
    px_per_mm_x, px_per_mm_y = large[0] / mm_width, large[1] / mm_height

    for label in labels:
        family = str(label["family"])
        weight = str(label["weight"])
        font_path = FONTS.get((family, weight), FONTS[("Barlow Condensed", "600")])
        font = ImageFont.truetype(str(font_path), round(float(label["size"]) * px_per_mm_y))
        horizontal = {"start": "l", "middle": "m", "end": "r"}.get(str(label["anchor"]), "l")
        vertical = "m" if label["baseline"] == "middle" else "s"
        color = ImageColor.getrgb(str(label["fill"])) + (round(255 * float(label["opacity"])),)
        draw_spaced(draw,
                    (float(label["x"]) * px_per_mm_x, float(label["y"]) * px_per_mm_y),
                    str(label["text"]), font, color,
                    float(label["spacing"]) * px_per_mm_x, horizontal + vertical)

    image = image.resize(target_size, Image.Resampling.LANCZOS).convert("RGB")
    target = ROOT / "assets" / f"{name}.png"
    image.save(target)
    print(f"{name}: {target_size[0]}x{target_size[1]}")


def main() -> None:
    for name, size in PANELS.items():
        render(name, size)


if __name__ == "__main__":
    main()
