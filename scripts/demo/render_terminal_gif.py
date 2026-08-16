#!/usr/bin/env python3
"""Render an actual terminal transcript as a compact animated GIF."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ANSI_ESCAPE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    for path in (
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
    ):
        if Path(path).is_file():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("transcript", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    text = ANSI_ESCAPE.sub("", args.transcript.read_text(encoding="utf-8", errors="replace"))
    lines = [line.rstrip().expandtabs(4)[:108] for line in text.splitlines() if line.strip()]
    if not lines:
        raise ValueError("transcript is empty")

    width, height = 960, 540
    margin, line_height, visible = 22, 20, 24
    terminal_font = font(15)
    frames: list[Image.Image] = []
    step = max(1, len(lines) // 50)
    first_end = min(8, len(lines))
    frame_ends = list(range(first_end, len(lines) + 1, step))
    if frame_ends[-1] != len(lines):
        frame_ends.append(len(lines))
    for end in frame_ends:
        image = Image.new("RGB", (width, height), "#111418")
        draw = ImageDraw.Draw(image)
        draw.rectangle((0, 0, width, 34), fill="#252a31")
        draw.text((margin, 8), "cpp_robot_middleware / actual demo", font=terminal_font,
                  fill="#e5e7eb")
        window = lines[max(0, end - visible):end]
        for index, line in enumerate(window):
            color = "#86efac" if "PASS" in line else "#d1d5db"
            draw.text((margin, 50 + index * line_height), line, font=terminal_font, fill=color)
        frames.append(image)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        args.output, save_all=True, append_images=frames[1:], duration=180,
        loop=0, optimize=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
