#!/usr/bin/env python3
"""Generate canonical Kurkesmurfer backgrounds for SpaceTime VCV panels."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / "vcv" / "res"
LOGO = RES / "kurkesmurfer-panel-logo.svg"
PALETTES = {
    "dark": ("#1d1713", "#6e6150", "#362c24", "#50463c", "0.5", "#262019"),
    "light": ("#e7e4de", "#aaa39a", "#70685f", "#9a9288", "0.7", "#d5d1ca"),
}
MODULES = {
    "Stage4": (50.8, 18.40, 13.02, "full"),
    "Head": (50.8, 14.20, 21.77, "full"),
    "HeadAll": (50.8, 23.27, 17.61, "full"),
    "Program": (91.44, 24.16, 22.82, "full"),
    "Midi": (20.32, 0, 0, "compact"),
    "GlueLeft": (10.16, 0, 0, "glue-left"),
    "GlueRight": (10.16, 0, 0, "glue-right"),
}

def logo_inner():
    svg = LOGO.read_text()
    return svg[svg.find(">") + 1:svg.rfind("</svg>")].strip()

def logo(width, title_width=None):
    h, w = 5.2, 5.2 * 5.41 / 10.94
    left = (width - w) / 2 if title_width is None else (width - w - 1.2 - title_width) / 2
    top, scale = 3.2, h / 277.0
    x = left + (w - 129 * scale) / 2
    return [
        f'<defs><clipPath id="logo-clip"><rect x="{left:.4f}" y="{top:.4f}" width="{w:.4f}" height="{h:.4f}"/></clipPath></defs>',
        f'<g clip-path="url(#logo-clip)"><g transform="translate({x:.4f},{top+5*scale:.4f}) scale({scale:.7f})">{logo_inner()}</g></g>',
    ]

def box(lines, x1, x2, y, h, color, opacity):
    lines.append(f'<rect x="{x1}" y="{y}" width="{x2-x1:.3f}" height="{h}" rx="1.8" fill="none" stroke="{color}" stroke-width="0.3" opacity="{opacity}"/>')

def render(name, width, title_width, subtitle_width, profile, theme):
    bg, screw, ring, zone, opacity, inset = PALETTES[theme]
    lines = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}mm" height="128.5mm" viewBox="0 0 {width} 128.5">', f'<rect width="{width}" height="128.5" fill="{bg}"/>']
    if not profile.startswith("glue"):
        for x in (3, width - 3):
            for y in (3.5, 125):
                lines.append(f'<circle cx="{x}" cy="{y}" r="1.5" fill="{screw}" stroke="{ring}" stroke-width="0.4"/>')
    if profile == "full":
        lines.extend(logo(width, title_width))
        gap, center = subtitle_width / 2 + 0.7, width / 2
        lines += [f'<path d="M 3 10.185 H {center-gap:.3f}" fill="none" stroke="#c13c36" stroke-width="0.27" opacity="0.7"/>', f'<path d="M {center+gap:.3f} 10.185 H {width-3:.3f}" fill="none" stroke="#c13c36" stroke-width="0.27" opacity="0.7"/>']
    else:
        lines.extend(logo(width))
        lines.append(f'<path d="M {width/2-3:.3f} 10.2 H {width/2+3:.3f}" fill="none" stroke="#c13c36" stroke-width="0.27" opacity="0.7"/>')

    if name == "Stage4":
        box(lines, 3.0, 47.8, 13.0, 40.5, zone, opacity)
        box(lines, 3.0, 47.8, 54.5, 16.8, zone, opacity)
        box(lines, 3.0, 47.8, 72.3, 49.2, zone, opacity)
        for x in (4.7, 15.7, 26.7, 37.7):
            lines.append(f'<rect x="{x}" y="55.6" width="9.4" height="12.0" rx="1.2" fill="{inset}" stroke="{ring}" stroke-width="0.25"/>')
    elif name in ("Head", "HeadAll"):
        for y, h in ((13.0, 22.5), (36.2, 18.6), (55.5, 29.0)):
            box(lines, 3.2, 47.6, y, h, zone, opacity)
        if name == "Head":
            box(lines, 3.2, 47.6, 85.5, 37.7, zone, opacity)
        else:
            box(lines, 3.2, 47.6, 85.5, 20.0, zone, opacity)
            box(lines, 3.2, 47.6, 106.3, 13.7, zone, opacity)
    elif name == "Program":
        for y, h in ((12.0, 15.2), (28.5, 36.0), (65.5, 21.0), (87.5, 15.5), (103.7, 19.5)):
            box(lines, 2.0, 73.0, y, h, zone, opacity)
        box(lines, 74.5, 89.7, 12.0, 41.5, zone, opacity)
        box(lines, 74.5, 89.7, 55.0, 31.5, zone, opacity)
        box(lines, 74.5, 89.7, 103.7, 17.8, zone, opacity)
        lines.append(f'<rect x="8" y="16.5" width="12" height="7" rx="0.8" fill="{inset}" stroke="{ring}" stroke-width="0.3"/>')
    elif name == "Midi":
        for y, h in ((41.0, 34.5), (77.5, 19.0), (98.5, 12.5), (112.0, 9.5)):
            box(lines, 1.35, 18.97, y, h, zone, opacity)
    else:
        box(lines, 1.0, 9.16, 42.0, 33.5, zone, opacity)
        if profile == "glue-left":
            path = "M2 78.25 L5.2 75.4 V77.3 H8.2 V79.2 H5.2 V81.1 Z"
        else:
            path = "M8.2 78.25 L5 75.4 V77.3 H2 V79.2 H5 V81.1 Z"
        lines.append(f'<path d="{path}" fill="#c13c36" opacity="0.8"/>')
    lines.append('</svg>')
    suffix = '-light' if theme == 'light' else ''
    (RES / f'{name}{suffix}.svg').write_text('\n'.join(lines) + '\n')

def main():
    for name, values in MODULES.items():
        for theme in PALETTES:
            render(name, *values, theme)

if __name__ == '__main__':
    main()
