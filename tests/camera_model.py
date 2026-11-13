"""Independent floating-point camera model that produces synthetic star lists and images."""
from __future__ import annotations

import math
import random

from native import MAX_STARS, H, W

SENSORS_MM = {"6D": 35.8, "600D": 22.3}
FOCALS_MM = (14, 16, 20, 24, 28, 35, 50, 85, 135, 200, 300)
SCALE_TOLERANCE = 1.3  # the camera's focal-hint window is +-30 %


class Camera:
    """Gnomonic lens with roll, optional mirror image and barrel distortion (k1 < 0)."""

    def __init__(self, ra, dec, roll, mirror, sensor_mm, focal_mm, k1=0.0):
        self.ra, self.dec, self.mirror, self.k1 = ra, dec, mirror, k1
        self.cr, self.sr = math.cos(math.radians(roll)), math.sin(math.radians(roll))
        self.k = W * focal_mm / sensor_mm  # pixels per tangent unit
        self.fov = math.degrees(2 * math.atan(sensor_mm / (2 * focal_mm)))

    def project(self, ra, dec):
        a0, d0 = math.radians(self.ra), math.radians(self.dec)
        a, d = math.radians(ra), math.radians(dec)
        cosc = math.sin(d0) * math.sin(d) + math.cos(d0) * math.cos(d) * math.cos(a - a0)
        if cosc <= 0.05:
            return None
        xi = math.cos(d) * math.sin(a - a0) / cosc
        eta = (math.cos(d0) * math.sin(d) - math.sin(d0) * math.cos(d) * math.cos(a - a0)) / cosc
        u = xi * self.cr - eta * self.sr
        v = xi * self.sr + eta * self.cr
        if self.mirror:
            u = -u
        r2 = (u * u + v * v) * (self.k / (W / 2)) ** 2
        f = 1 + self.k1 * r2
        return W / 2 - self.k * u * f, H / 2 - self.k * v * f


def random_camera(rng: random.Random, sensor_mm: float, focal_mm: int) -> Camera:
    ra = rng.uniform(0, 360)
    dec = math.degrees(math.asin(rng.uniform(-1, 1)))
    k1 = -0.04 if focal_mm <= 24 else (-0.015 if focal_mm <= 50 else 0.0)
    return Camera(ra, dec, rng.uniform(-180, 180), rng.random() < 0.5, sensor_mm, focal_mm, k1)


def scale_window(sensor_mm: float, focal_mm: int) -> tuple[int, int]:
    """Q16 pixels per millidegree expected for a lens, +-30 %."""
    s = W * focal_mm / sensor_mm * math.pi / 180000 * 65536
    return int(s / SCALE_TOLERANCE), int(s * SCALE_TOLERANCE)


def limiting_mag(focal_mm: int, rng: random.Random) -> float:
    """Short Live View frames: wide lenses see bright stars only."""
    return min(8.0, max(4.3, 5.4 + 2.5 * math.log10(focal_mm / 50))) + rng.uniform(-0.4, 0.4)


def visible_stars(stars, cam: Camera, maglim: float):
    """(mag, x, y) of catalogue stars inside the frame, brightest first."""
    out = []
    near = math.radians(cam.fov) * 0.9 + 0.2
    dr, rr = math.radians(cam.dec), math.radians(cam.ra)
    c0 = (math.cos(dr) * math.cos(rr), math.cos(dr) * math.sin(rr), math.sin(dr))
    cmin = math.cos(near)
    for ra, dec, mag, _ in stars:
        if mag > maglim:
            continue
        r, d = math.radians(ra), math.radians(dec)
        dot = math.cos(d) * math.cos(r) * c0[0] + math.cos(d) * math.sin(r) * c0[1] \
            + math.sin(d) * c0[2]
        if dot < cmin:
            continue
        p = cam.project(ra, dec)
        if p and 3 <= p[0] < W - 3 and 3 <= p[1] < H - 3:
            out.append((mag, p[0], p[1]))
    out.sort()
    return out


def detections(visible, rng: random.Random, maglim: float, false_stars=2, drop=0.1,
               noise=0.35):
    """Simulated detector output: noise, misses and false positives, at most MAX_STARS."""
    found = [(mag, x + rng.gauss(0, noise), y + rng.gauss(0, noise))
             for mag, x, y in visible if rng.random() > drop]
    for _ in range(false_stars):
        found.append((rng.uniform(maglim - 3, maglim), rng.uniform(3, W - 3),
                      rng.uniform(3, H - 3)))
    found.sort()
    return found[:MAX_STARS]


def render(visible, rng: random.Random, sigma=1.2, background=20, noise=3.0,
           trail=None) -> bytes:
    """8-bit grayscale W x H image with Gaussian stars and an optional bright trail."""
    image = [background + rng.gauss(0, noise) for _ in range(W * H)]
    for mag, x, y in visible:
        peak = min(235.0, 2000.0 * 10 ** (-0.4 * mag) + 40)
        for yy in range(max(0, int(y) - 5), min(H, int(y) + 6)):
            for xx in range(max(0, int(x) - 5), min(W, int(x) + 6)):
                r2 = (xx - x) ** 2 + (yy - y) ** 2
                image[yy * W + xx] += peak * math.exp(-r2 / (2 * sigma * sigma))
    if trail:
        (x0, y0), (x1, y1) = trail
        steps = int(math.hypot(x1 - x0, y1 - y0) * 2)
        for i in range(steps + 1):
            px = x0 + (x1 - x0) * i / steps
            py = y0 + (y1 - y0) * i / steps
            for dy in (-1, 0, 1):
                xi, yi = int(px), int(py) + dy
                if 0 <= xi < W and 0 <= yi < H:
                    image[yi * W + xi] = 220
    return bytes(max(0, min(255, int(v))) for v in image)
