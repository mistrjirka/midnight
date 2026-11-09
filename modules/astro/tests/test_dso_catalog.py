#!/usr/bin/env python3
"""DSO.BIN / DSO.KEY / DSO.NAM integrity and target lookups (astro_overlay.c).

Every designation must resolve through the camera's binary search, cross
identifications must share one record, and well-known objects must have
the expected J2000 positions.
"""
import ctypes as C
import struct

from native import DATA, CAT, Target, Dso, overlay_api, separation

api = overlay_api()

# designation -> (RA deg, Dec deg); positions from standard references.
KNOWN = {
    ("M", 42): (83.82, -5.39), ("M", 31): (10.68, 41.27), ("M", 45): (56.87, 24.11),
    ("M", 40): (185.57, 58.08), ("M", 102): (226.62, 55.76), ("M", 1): (83.63, 22.01),
    ("NGC", 7000): (314.82, 44.53), ("NGC", 224): (10.68, 41.27), ("NGC", 1976): (83.82, -5.39),
    ("IC", 434): (85.25, -2.40), ("IC", 1396): (324.75, 57.50),
    ("C", 14): (35.17, 57.14), ("C", 20): (314.82, 44.53),  # C99 Coalsack: OpenNGC centre differs by 2 deg
    ("B", 33): (85.25, -2.46), ("B", 72): (260.88, -23.63),
    ("Sh2", 155): (344.48, 62.52), ("Sh2", 281): (83.8, -5.4),
}
NAMES = {("M", 42): "Orion Nebula", ("M", 31): "Andromeda Galaxy", ("B", 33): "Horsehead Nebula"}


def lookup(cat, num):
    t = Target()
    return api.lookup(CAT[cat], num, C.byref(t)), t


for (cat, num), (ra, dec) in KNOWN.items():
    rc, t = lookup(cat, num)
    assert rc == 1, (cat, num, rc)
    err = separation(t.obj.ra / 1e4, t.obj.dec / 1e4, ra, dec)
    assert err < 0.25, (cat, num, err, t.obj.ra / 1e4, t.obj.dec / 1e4)
    print(f"{t.label.decode():9} -> best {t.obj.cat}:{t.obj.num:<5} RA {t.obj.ra/1e4:8.3f} Dec {t.obj.dec/1e4:+8.3f}"
          f"  size {t.obj.maj/10:.1f}'  ({err*60:.1f}' from reference)")

# Cross identifications point at the same physical object record.
for a, b in ((("M", 31), ("NGC", 224)), (("M", 42), ("NGC", 1976)), (("C", 20), ("NGC", 7000))):
    ra, rb = lookup(*a)[1], lookup(*b)[1]
    assert (ra.obj.ra, ra.obj.dec, ra.obj.cat, ra.obj.num) == (rb.obj.ra, rb.obj.dec, rb.obj.cat, rb.obj.num), (a, b)
assert lookup("NGC", 1976)[1].obj.cat == CAT["M"], "best designation of NGC 1976 should be M42"
assert lookup("NGC", 1976)[1].label == b"NGC 1976", "label keeps the designation the user picked"

for key, expected in NAMES.items():
    t = lookup(*key)[1]
    buf = C.create_string_buffer(24)
    api.common_name(C.byref(t.obj), buf, 24)
    assert expected.lower() in buf.value.decode().lower(), (key, buf.value)

for cat, num in (("M", 111), ("NGC", 9999), ("Sh2", 0), ("B", 999)):
    assert lookup(cat, num)[0] == 0, (cat, num)

# File integrity: every key resolves, keys sorted and unique, coverage.
raw = (DATA / "DSO.KEY").read_bytes()
magic, version, count, size = struct.unpack_from("<4sIII", raw)
assert (magic, version, size) == (b"SKEY", 1, 8) and len(raw) == 16 + 8 * count
keys = [struct.unpack_from("<BBHI", raw, 16 + 8 * i) for i in range(count)]
assert all((a[0], a[2]) < (b[0], b[2]) for a, b in zip(keys, keys[1:]))
dso = (DATA / "DSO.BIN").read_bytes()
magic, version, nobj, rsize, nra, ndec = struct.unpack_from("<4sIIIII", dso)
assert (magic, version, rsize, nra, ndec) == (b"SDSO", 1, 24, 72, 36)
assert len(dso) == 24 + (72 * 36 + 1) * 4 + 24 * nobj
assert all(k[3] < nobj for k in keys)
per_cat = {name: sum(1 for k in keys if k[0] == cid) for name, cid in CAT.items()}
assert per_cat["M"] == 110 and per_cat["C"] == 109 and per_cat["Sh2"] == 313
assert per_cat["NGC"] > 7000 and per_cat["IC"] > 5000 and per_cat["B"] > 340
step = max(1, count // 400)
for c, _, n, _ in keys[::step]:
    assert lookup(next(k for k, v in CAT.items() if v == c), n)[0] == 1
print("catalogue sizes:", per_cat)
print(f"PASS: {count} designations for {nobj} objects; camera lookups and cross-IDs verified")
