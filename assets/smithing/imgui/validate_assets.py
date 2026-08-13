#!/usr/bin/env python3
"""Validate atlas coordinates, source PNGs, alpha and nine-slice fragments."""

from __future__ import annotations

import json
import re
from pathlib import Path

from PIL import Image, ImageChops


ROOT = Path(__file__).resolve().parent
MANIFEST = ROOT / "integration" / "smithing_imgui_assets.json"
REPORT = ROOT / "integration" / "validation.json"


def main():
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    atlas = Image.open(ROOT / data["atlas"]["file"]).convert("RGBA")
    errors = []
    warnings = []
    rects = []

    if atlas.size != tuple(data["atlas"]["size"]):
        errors.append(f"Atlas size is {atlas.size}, manifest says {data['atlas']['size']}")

    for key, sprite in data["sprites"].items():
        path = ROOT / sprite["file"]
        if not path.is_file():
            errors.append(f"Missing source: {sprite['file']}")
            continue
        source = Image.open(path)
        if source.mode != "RGBA":
            errors.append(f"{sprite['file']} is {source.mode}, expected RGBA")
        x, y, w, h = sprite["src"]
        if source.size != (w, h):
            errors.append(f"{sprite['file']} is {source.size}, rect is {(w,h)}")
        if x < 0 or y < 0 or x + w > atlas.width or y + h > atlas.height:
            errors.append(f"Atlas rect out of bounds: {key}")
            continue
        crop = atlas.crop((x, y, x + w, y + h))
        if ImageChops.difference(crop, source.convert("RGBA")).getbbox() is not None:
            errors.append(f"Atlas pixels differ from source: {key}")
        rects.append((key, x, y, x + w, y + h))

    for i, (ka, ax0, ay0, ax1, ay1) in enumerate(rects):
        for kb, bx0, by0, bx1, by1 in rects[i + 1:]:
            if max(ax0, bx0) < min(ax1, bx1) and max(ay0, by0) < min(ay1, by1):
                errors.append(f"Overlapping atlas rects: {ka} and {kb}")

    fragment_count = 0
    for name, spec in data["nine_slice"].items():
        key = spec["sprite"]
        source_file = ROOT / data["sprites"][key]["file"]
        source = Image.open(source_file).convert("RGBA")
        directory = ROOT / "assets" / "chrome" / f"{name}_9slice"
        names_y = ("top", "middle", "bottom")
        names_x = ("left", "center", "right")
        border = spec["border"][0]
        xs = (0, border, source.width - border, source.width)
        ys = (0, border, source.height - border, source.height)
        rebuilt = Image.new("RGBA", source.size)
        for yi in range(3):
            for xi in range(3):
                part_path = directory / f"{names_y[yi]}_{names_x[xi]}.png"
                if not part_path.is_file():
                    errors.append(f"Missing nine-slice fragment: {part_path.relative_to(ROOT)}")
                    continue
                part = Image.open(part_path).convert("RGBA")
                expected = (xs[xi + 1] - xs[xi], ys[yi + 1] - ys[yi])
                if part.size != expected:
                    errors.append(f"Wrong fragment size: {part_path.relative_to(ROOT)}")
                rebuilt.paste(part, (xs[xi], ys[yi]))
                fragment_count += 1
        if ImageChops.difference(rebuilt, source).getbbox() is not None:
            errors.append(f"Nine-slice fragments do not rebuild {name}")

    generated_header = (ROOT / "integration" / "SmithingUiAtlas.generated.h").read_text(encoding="utf-8")
    constant_count = len(re.findall(r"inline constexpr Rect ", generated_header))
    if constant_count != len(data["sprites"]):
        errors.append(f"C++ constants: {constant_count}, manifest sprites: {len(data['sprites'])}")

    report = {
        "schema": "remeslo-stredoveku.smithing-imgui-validation.v1",
        "status": "passed" if not errors else "failed",
        "atlas_size": list(atlas.size),
        "atlas_mode": atlas.mode,
        "atlas_entries": len(data["sprites"]),
        "standalone_runtime_png": len(data["sprites"]),
        "nine_slice_fragments": fragment_count,
        "cpp_rect_constants": constant_count,
        "errors": errors,
        "warnings": warnings,
    }
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    raise SystemExit(1 if errors else 0)


if __name__ == "__main__":
    main()
