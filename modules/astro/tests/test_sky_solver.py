#!/usr/bin/env python3
"""Blind plate-solver regression on synthetic HYG star fields.

Runs the camera's own astro_sky.c (host build) against star lists produced
by an independent floating-point camera model: random pointing over the whole
sky, roll, mirror parity, both sensors, 14-300 mm lenses, centroid noise,
missed and false detections, and barrel distortion on wide lenses.

A wrong solution is worse than none, so ANY wrong answer fails the test.
Pass rates (>=85 %) are enforced for 9-55 degree fields (default index).
Wider lenses are reported only: they use the optional SKYW.* index (--wide),
and blind solving such huge fields is a convenience, not a requirement.
Usage: test_sky_solver.py [--trials N] [--report] [--wide]  (needs .astro-data/SKY.*)
"""
from __future__ import annotations
import argparse
import ctypes as C
import math
import random
import statistics
import sys
import time

from native import (W, H, MAX_STARS, Star, SkySolution, SkyOptions, sky_api, wcs_api,
                    load_stars, separation)

SENSORS = {"600D": 22.3, "6D": 35.8}
FOCALS = (14, 16, 20, 24, 28, 35, 50, 85, 135, 200, 300)
SCALE_TOLERANCE = 1.3  # focal-hint window used by the camera (+-30 %)


class Camera:
    """Floating-point reference camera: gnomonic lens + optional distortion."""

    def __init__(self, ra, dec, roll, mirror, sensor, focal, k1=0.0):
        self.ra, self.dec, self.mirror, self.k1 = ra, dec, mirror, k1
        self.cr, self.sr = math.cos(math.radians(roll)), math.sin(math.radians(roll))
        self.k = W * focal / sensor  # pixels per unit tangent
        self.fov = math.degrees(2 * math.atan(sensor / (2 * focal)))

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


def scale_window(sensor, focal):
    """Q16 pixels per millidegree expected for a lens, +-30 %."""
    px_per_mdeg = W * focal / sensor * math.pi / 180000
    s = px_per_mdeg * 65536
    return int(s / SCALE_TOLERANCE), int(s * SCALE_TOLERANCE)


def limiting_mag(focal, rng):
    # Short Live View frames: wide lenses see bright stars only.
    return min(8.0, max(4.3, 5.4 + 2.5 * math.log10(focal / 50))) + rng.uniform(-0.4, 0.4)


def synthetic_frame(stars, cam, rng, maglim, false_stars=2, drop=0.1, noise=0.35):
    visible = []
    near = math.radians(cam.fov) * 0.9 + 0.2
    c0 = (math.cos(math.radians(cam.dec)) * math.cos(math.radians(cam.ra)),
          math.cos(math.radians(cam.dec)) * math.sin(math.radians(cam.ra)),
          math.sin(math.radians(cam.dec)))
    cmin = math.cos(near)
    for ra, dec, mag, _ in stars:
        if mag > maglim:
            continue
        r, d = math.radians(ra), math.radians(dec)
        if math.cos(d) * math.cos(r) * c0[0] + math.cos(d) * math.sin(r) * c0[1] + math.sin(d) * c0[2] < cmin:
            continue
        p = cam.project(ra, dec)
        if p and 3 <= p[0] < W - 3 and 3 <= p[1] < H - 3:
            visible.append((mag, p[0], p[1]))
    visible.sort()
    detections = [(mag, x + rng.gauss(0, noise), y + rng.gauss(0, noise))
                  for mag, x, y in visible if rng.random() > drop]
    for _ in range(false_stars):
        detections.append((rng.uniform(maglim - 3, maglim), rng.uniform(3, W - 3), rng.uniform(3, H - 3)))
    detections.sort()
    return visible, detections[:MAX_STARS]


def solve(api, detections, opt=None):
    arr = (Star * len(detections))(*(Star(round(x), round(y), max(1, 60000 - i * 100))
                                     for i, (_, x, y) in enumerate(detections)))
    sol = SkySolution()
    rc = api.run_sky(arr, len(detections), W, H, C.byref(opt) if opt else None, C.byref(sol))
    return rc, sol


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trials", type=int, default=12, help="frames per lens and sensor")
    ap.add_argument("--report", action="store_true", help="print rates, do not enforce them")
    ap.add_argument("--focals", default=",".join(map(str, FOCALS)))
    ap.add_argument("--hint", action="store_true", help="also solve with the focal-length window")
    ap.add_argument("--wide", action="store_true", help="fall back to the optional SKYW.* index")
    args = ap.parse_args()
    focals = [int(f) for f in args.focals.split(",")]
    stars = load_stars()
    bright = [(ra, dec) for ra, dec, mag, _ in stars if mag <= 5.5]
    api, wapi = sky_api(), wcs_api()
    wide = sky_api(prefix="SKYW") if args.wide else None
    rng = random.Random(20261109)
    wrong = []
    rates = {}
    reproj = []
    work = {"blind": [], "hint": []}
    started = time.monotonic()
    for sensor_name, sensor in SENSORS.items():
        for focal in focals:
            ok = attempted = 0
            for _ in range(args.trials):
                ra = rng.uniform(0, 360)
                dec = math.degrees(math.asin(rng.uniform(-1, 1)))
                k1 = -0.04 if focal <= 24 else (-0.015 if focal <= 50 else 0.0)
                cam = Camera(ra, dec, rng.uniform(-180, 180), rng.random() < 0.5,
                             sensor, focal, k1)
                _, det = synthetic_frame(stars, cam, rng, limiting_mag(focal, rng))
                if len(det) < 8:
                    continue
                attempted += 1
                modes = [("blind", None)]
                if args.hint:
                    lo, hi = scale_window(sensor, focal)
                    modes.append(("hint", SkyOptions(lo, hi)))
                for mode, opt in modes:
                    rc, sol = solve(api, det, opt)
                    work[mode].append(sol.checked)
                    if rc == 0 and wide:
                        rc, sol = solve(wide, det, opt)
                    if rc != 1:
                        continue
                    err = separation(sol.ra / 1e4, sol.dec / 1e4, ra, dec)
                    fov = sol.fov_cdeg / 100
                    if err > 0.1 + 0.05 * cam.fov or abs(fov - cam.fov) > 0.15 * cam.fov:
                        wrong.append((sensor_name, focal, mode, round(ra, 2), round(dec, 2),
                                      round(err, 3), round(fov, 2), round(cam.fov, 2)))
                        continue
                    if mode != "blind":
                        continue
                    ok += 1
                    # The returned WCS must place catalogue stars on the image.
                    errs = []
                    for sra, sdec in bright:
                        p = cam.project(sra, sdec)
                        if not p or not (20 < p[0] < W - 20 and 20 < p[1] < H - 20):
                            continue
                        x16, y16 = C.c_int32(), C.c_int32()
                        if wapi.astro_wcs_project(C.byref(sol.wcs), round(sra * 1e4) % 3600000,
                                                  round(sdec * 1e4), C.byref(x16), C.byref(y16)):
                            errs.append(math.hypot(x16.value / 16 - p[0], y16.value / 16 - p[1]))
                    if errs:
                        reproj.append((focal, statistics.median(errs)))
            rates[(sensor_name, focal)] = (ok, attempted)
            print(f"{sensor_name:4} {focal:3d} mm  FOV {math.degrees(2*math.atan(sensor/(2*focal))):5.1f} deg: "
                  f"solved {ok:2d}/{attempted:2d}", flush=True)
    for name, values in work.items():
        if values:
            print(f"{name:5} mean verification checks per frame: {statistics.mean(values):7.1f}")
    narrow = [e for f, e in reproj if f >= 35]
    if narrow:
        print(f"WCS reprojection median (>=35 mm, undistorted centre): {statistics.median(narrow):.2f} px")
    # Random point clouds must never produce an answer.
    noise_rng = random.Random(77)
    false_hits = 0
    for _ in range(60):
        det = [(0, noise_rng.uniform(2, W - 2), noise_rng.uniform(2, H - 2)) for _ in range(28)]
        rc, _ = solve(api, det)
        false_hits += rc == 1
    print(f"random fields accepted: {false_hits}/60   ({time.monotonic()-started:.0f} s)")
    if wrong:
        print("WRONG SOLUTIONS:", *wrong, sep="\n  ")
    if args.report:
        return
    assert not wrong, "solver returned a wrong sky position"
    assert false_hits == 0, "solver accepted a random field"
    for (sensor_name, focal), (ok, attempted) in rates.items():
        fov = math.degrees(2 * math.atan(SENSORS[sensor_name] / (2 * focal)))
        if attempted >= 4 and 9 <= fov <= 55:
            assert attempted - ok <= max(1, math.ceil(0.15 * attempted)), (sensor_name, focal, ok, attempted)
    if narrow:
        assert statistics.median(narrow) < 1.5
    print("PASS: blind solver, 9-55 degree fields on 600D and 6D, zero wrong answers")


if __name__ == "__main__":
    sys.exit(main())
