#!/usr/bin/env python3
"""Read the canonical camera profiles also compiled into the ARM module.

Format is restricted. This is a static manifest, not a general C parser.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re

MANIFEST = Path(__file__).resolve().parents[1] / "modules/astro/camera_profiles.def"
PROFILE = re.compile(
    r'^ASTRO_CAMERA\("([A-Za-z0-9]+)", "([0-9]+\.[0-9]+\.[0-9]+)", '
    r'(\d+), (-?\d+), "([a-zA-Z0-9_.-]+)"\)$'
)


@dataclass(frozen=True)
class Camera:
    model: str
    firmware: str
    sensor_width_x10: int
    raw_focus_key: int
    symbol_file: str

    @property
    def platform(self) -> str:
        return f"{self.model}.{self.firmware.replace('.', '')}"


def load_cameras() -> tuple[Camera, ...]:
    cameras: list[Camera] = []
    for n, raw in enumerate(MANIFEST.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("/*") or line.startswith("*") or line == "*/":
            continue
        match = PROFILE.fullmatch(line)
        if not match:
            raise ValueError(f"Invalid camera record {MANIFEST}:{n}: {line}")
        model, firmware, width, key, symbol = match.groups()
        cameras.append(Camera(model, firmware, int(width), int(key), symbol))

    if not cameras or len({c.platform for c in cameras}) != len(cameras):
        raise ValueError("Missing or duplicated camera/firmware entries")
    if not all(0 < c.sensor_width_x10 < 1000 for c in cameras):
        raise ValueError("Invalid sensor width in camera manifest")
    if not all(c.raw_focus_key >= -1 for c in cameras):
        raise ValueError("Invalid raw key in camera manifest")
    return tuple(cameras)


if __name__ == "__main__":
    for camera in load_cameras():
        print(f"{camera.platform}\t{camera.symbol_file}")
