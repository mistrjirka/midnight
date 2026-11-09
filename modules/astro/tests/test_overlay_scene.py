#!/usr/bin/env python3
"""Overlay scene builder (astro_overlay.c) on known sky fields.

Uses an exact camera WCS (no solver) so every expectation is geometric:
Orion with a 35 mm lens on the 600D must show M42 at its true pixel, label
famous stars by name and Bayer stars with a Greek-letter byte, draw Orion's
figure and point an arrow at an off-frame target in the right direction.
"""
import ctypes as C
import math

from native import (W, H, CAT, LABEL_STAR, LABEL_DSO, LABEL_CONSTELLATION, LABEL_TARGET,
                    Target, OverlayOpts, Scene, overlay_api, camera_wcs, local_direction,
                    separation)
from test_sky_solver import Camera

api = overlay_api()
OPTS = OverlayOpts(1, 1, 1, 45, 100)


def build(cam, target=None, opts=OPTS):
    wcs = camera_wcs(cam.ra, cam.dec, math.degrees(math.atan2(cam.sr, cam.cr)), cam.mirror, cam.k)
    scene = Scene()
    rc = api.scene(C.byref(wcs), W, H, C.byref(opts), C.byref(target) if target else None, C.byref(scene))
    assert rc == 0
    return scene


def pick(cat, num):
    t = Target()
    assert api.lookup(CAT[cat], num, C.byref(t)) == 1
    return t


def labels(scene, kind=None):
    return [scene.labels[i] for i in range(scene.nlabels) if kind is None or scene.labels[i].kind == kind]


for mirror in (False, True):
    cam = Camera(85.0, 0.0, 10, mirror, 22.3, 35)
    m42 = pick("M", 42)
    scene = build(cam, m42)
    stars = {l.text for l in labels(scene, LABEL_STAR)}
    names = {l.text for l in labels(scene)}
    assert scene.target == 1 and scene.target_label == b"M42"
    ref = cam.project(m42.obj.ra / 1e4, m42.obj.dec / 1e4)
    assert math.hypot(scene.target_x / 16 - ref[0], scene.target_y / 16 - ref[1]) < 0.2
    assert {b"Betelgeuse", b"Rigel"} <= stars, stars
    # Mintaka (mag 2.2) is stored as Bayer delta: Greek byte 0x04 + " Ori".
    assert b"\x04 Ori" in stars, stars
    assert b"Orion" in {l.text for l in labels(scene, LABEL_CONSTELLATION)}
    assert {b"M78", b"IC 434"} & {l.text for l in labels(scene, LABEL_DSO)}
    assert scene.nlines >= 8, scene.nlines
    target_ellipse = [scene.ellipses[i] for i in range(scene.nellipses) if scene.ellipses[i].kind == LABEL_TARGET]
    assert len(target_ellipse) == 1
    e = target_ellipse[0]
    semi_major = math.hypot(e.ax, e.ay) / 16
    expected = cam.k * math.radians(m42.obj.maj / 600 / 2)
    assert abs(semi_major - expected) < 0.05 * expected + 0.2, (semi_major, expected)
    assert labels(scene)[0].kind == LABEL_TARGET, "target label must be placed first"
    for i in range(scene.nlines):
        l = scene.lines[i]
        assert all(0 <= v < W * 16 for v in (l.x1, l.x2)) and all(0 <= v < H * 16 for v in (l.y1, l.y2))
    print(f"Orion {'mirrored' if mirror else 'normal  '}: {scene.nlines} lines, {scene.nellipses} outlines, "
          f"{scene.nlabels} labels: {sorted(n.decode('latin-1') for n in names)[:12]} ...")

    # Off-frame target: arrow along the great circle towards M31.
    m31 = pick("M", 31)
    far = build(cam, m31)
    assert far.target == 2
    rx, ry = local_direction(cam, cam.ra, cam.dec, m31.obj.ra / 1e4, m31.obj.dec / 1e4)
    angle = math.degrees(math.acos(max(-1, min(1, (far.target_dx * rx + far.target_dy * ry) / 16384))))
    assert angle < 1.0, angle
    ref_sep = separation(cam.ra, cam.dec, m31.obj.ra / 1e4, m31.obj.dec / 1e4)
    assert abs(far.target_sep / 1e4 - ref_sep) < 0.01, (far.target_sep, ref_sep)
    print(f"  M31 off frame: {far.target_sep/1e4:.1f} deg, arrow error {angle:.2f} deg")

# Layer switches and a polar field (RA wrap, all tiles) must work.
empty = build(Camera(84.0, -2.0, 0, False, 22.3, 50), None, OverlayOpts(0, 0, 0, 45, 100))
assert empty.nlines == empty.nlabels == empty.nellipses == 0 and empty.target == 0
polar = build(Camera(10.0, 88.0, 0, False, 22.3, 24), pick("C", 1))
assert b"Polaris" in {l.text for l in labels(polar, LABEL_STAR)}
assert polar.target == 1
print(f"Polar field: {polar.nlines} lines, labels {[l.text for l in labels(polar)][:8]}")
print("PASS: scene geometry, Greek Bayer labels, constellations, outlines and target arrow")
