"""Hardware-free first pass at the UGV's flag position detector."""

from __future__ import annotations

import argparse
import colorsys
import json
from collections import deque
from pathlib import Path

from PIL import Image, ImageDraw


# Hue is in degrees; the other thresholds are fractions of 1.
PRESETS = {
    "red": ((345, 360), (0, 15)),
    "orange": ((16, 44),),
    "yellow": ((45, 70),),
    "green": ((71, 165),),
    "blue": ((185, 260),),
    "purple": ((261, 344),),
}
MIN_SATURATION = 0.35
MIN_BRIGHTNESS = 0.18
MIN_PIXELS = 20  # On the 80 x 60 sampled image.


def matches(rgb: tuple[int, int, int], color: str) -> bool:
    hue, saturation, brightness = colorsys.rgb_to_hsv(*(v / 255 for v in rgb))
    degrees = hue * 360
    return (
        saturation >= MIN_SATURATION
        and brightness >= MIN_BRIGHTNESS
        and any(low <= degrees <= high for low, high in PRESETS[color])
    )


def detect(image: Image.Image, color: str) -> dict:
    """Return the largest 4-connected patch of the selected color."""
    color = color.lower()
    if color not in PRESETS:
        raise ValueError(f"Choose one of: {', '.join(PRESETS)}")
    image = image.convert("RGB").resize((160, 120))
    width, height = 80, 60
    pixels = image.load()
    mask = bytearray(
        matches(pixels[x * 2, y * 2], color)
        for y in range(height)
        for x in range(width)
    )
    best = None
    for start in range(width * height):
        if mask[start] != 1:
            continue
        queue = deque([start])
        mask[start] = 2
        count = sum_x = sum_y = 0
        left = top = 1_000
        right = bottom = 0
        while queue:
            index = queue.popleft()
            x, y = index % width, index // width
            count += 1
            sum_x += x
            sum_y += y
            left, right = min(left, x), max(right, x)
            top, bottom = min(top, y), max(bottom, y)
            for neighbor in (index - width, index + width, index - 1, index + 1):
                if neighbor < 0 or neighbor >= width * height:
                    continue
                nx, ny = neighbor % width, neighbor // width
                if abs(nx - x) + abs(ny - y) != 1 or mask[neighbor] != 1:
                    continue
                mask[neighbor] = 2
                queue.append(neighbor)
        if best is None or count > best[0]:
            best = count, sum_x, sum_y, (left, top, right, bottom)

    if best is None or best[0] < MIN_PIXELS:
        return {"color": color.upper(), "valid": False, "error_pct": 0,
                "area_permille": 0, "box": None}

    count, sum_x, _, box = best
    center_x = sum_x / count
    error = round((center_x - (width - 1) / 2) * 200 / (width - 1))
    return {"color": color.upper(), "valid": True, "error_pct": error,
            "area_permille": round(count * 1000 / (width * height)),
            "box": [v * 2 for v in box]}


def demo(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    cases = [
        ("red_left", "red", (18, 32, 59, 91), "#db3835"),
        ("green_center", "green", (59, 25, 100, 100), "#34aa58"),
        ("blue_right", "blue", (105, 35, 147, 92), "#2364c8"),
        ("yellow_small", "yellow", (72, 48, 89, 70), "#eed028"),
    ]
    for name, color, box, fill in cases:
        image = Image.new("RGB", (160, 120), "#e4e9e5")
        ImageDraw.Draw(image).rectangle(box, fill=fill)
        path = directory / f"{name}.png"
        image.save(path)
        print(path, json.dumps(detect(image, color)))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, help="Image to analyze")
    parser.add_argument("--color", choices=PRESETS, default="red")
    parser.add_argument("--output", type=Path, help="Save an annotated result")
    parser.add_argument("--demo", action="store_true", help="Create sample flags")
    args = parser.parse_args()
    if args.demo:
        demo(Path(__file__).parent / "demo")
    if args.image:
        image = Image.open(args.image)
        result = detect(image, args.color)
        print(json.dumps(result, indent=2))
        if args.output:
            view = image.convert("RGB").resize((160, 120))
            if result["box"]:
                ImageDraw.Draw(view).rectangle(result["box"], outline="black", width=2)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            view.save(args.output)
    if not args.demo and not args.image:
        parser.error("Provide --demo or --image")


if __name__ == "__main__":
    main()
