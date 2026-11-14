"""Verify Canon button codes using Magic Lantern's own module_translate_key C function.

This checks the exact pinned platform GUI headers, not copied or reimplemented
key translation logic. It cannot prove physical button delivery.
"""
from __future__ import annotations

import ctypes
from pathlib import Path
import subprocess
import tempfile

import pytest

ROOT = Path(__file__).resolve().parents[1]
ML = ROOT / ".build" / "magiclantern"
BUTTONS = {
    # Canon raw code, translated ML portable key.
    "550D.109": {
        0x0F: 29,  # Q
        0x04: 9,   # SET
        0x05: 10,  # release SET
        0x09: 24,  # playback
        0x07: 23,  # INFO
        0x0A: 25,  # Trash
        0x1C: 16,  # left
        0x1A: 15,  # right
        0x3F: 1,   # shutter half press
        0x40: 2,   # shutter release
    },
    "600D.102": {
        0x13: 29, 0x04: 9, 0x05: 10, 0x0B: 24,
        0x07: 23, 0x0D: 25, 0x25: 16, 0x23: 15,
        0x48: 1, 0x49: 2,
    },
    "700D.115": {
        0x1D: 29, 0x04: 9, 0x05: 10, 0x0B: 24,
        0x07: 23, 0x0D: 25, 0x26: 16, 0x24: 15,
        0x4E: 1, 0x4F: 2, 0x6B: 35, 0x6C: 36,
        0x72: 37, 0x73: 38,
    },
    "650D.104": {
        0x1D: 29, 0x04: 9, 0x05: 10, 0x0B: 24,
        0x07: 23, 0x0D: 25, 0x26: 16, 0x24: 15,
        0x4E: 1, 0x4F: 2, 0x6B: 35, 0x6C: 36,
        0x72: 37, 0x73: 38,
    },
    "100D.101": {
        0x24: 29,  # Av acts as Magic Lantern's Q key on this body
        0x04: 9, 0x05: 10, 0x0B: 24, 0x07: 23, 0x0D: 25,
        0x28: 16, 0x26: 15, 0x50: 1, 0x51: 2,
        0x6F: 35, 0x70: 36, 0x76: 37, 0x77: 38,
    },
    "60D.111": {
        0x19: 29, 0x04: 9, 0x05: 10, 0x0B: 24,
        0x07: 23, 0x0C: 25, 0x2A: 16, 0x26: 15,
        0x41: 1, 0x42: 2,
    },
    "1100D.105": {
        0x13: 29, 0x04: 9, 0x05: 10, 0x0B: 24,
        0x07: 23, 0x0D: 25, 0x25: 16, 0x23: 15,
        0x48: 1, 0x49: 2,
    },
    "70D.112": {
        0x1D: 29, 0x04: 9, 0x05: 10, 0x0B: 24,
        0x07: 23, 0x0D: 25, 0x34: 16, 0x30: 15,
        0x50: 1, 0x51: 2, 0x6F: 35, 0x70: 36,
        0x76: 37, 0x77: 38,
    },
    "6D.116": {
        0x1D: 29, 0x04: 9, 0x05: 10, 0x0B: 24,
        0x07: 23, 0x0D: 25, 0x50: 1, 0x51: 2,
    },
}

def test_real_magic_lantern_button_maps():
    module_c = ML / "src" / "module.c"
    module_h = ML / "src" / "module.h"
    if not module_c.is_file():
        pytest.skip("pinned Magic Lantern checkout not present")
    module_source = module_c.read_text()
    module_keys = module_h.read_text()
    preface = module_keys[
        module_keys.index("#define MODULE_KEY_PRESS_HALFSHUTTER"):
        module_keys.index("/* update major", module_keys.index("#define MODULE_KEY_PRESS_HALFSHUTTER"))
    ]
    actual_translator = module_source[
        module_source.index("/* translate camera specific key to portable module key */"):
        module_source.index("#undef MODULE_TRANSLATE_KEY") + len("#undef MODULE_TRANSLATE_KEY")
    ]
    # These three macros are defined by Magic Lantern's gui-common.h, not the
    # camera-specific gui.h. The real module.c includes both headers.
    common = """
#define BGMT_UNPRESS_HALFSHUTTER (BGMT_PRESS_HALFSHUTTER+1)
#define BGMT_PRESS_FULLSHUTTER (BGMT_PRESS_HALFSHUTTER+2)
#define BGMT_UNPRESS_FULLSHUTTER (BGMT_PRESS_HALFSHUTTER+3)
"""
    with tempfile.TemporaryDirectory(prefix="midnight-button-maps-") as tmp:
        base = Path(tmp)
        for camera, expected in BUTTONS.items():
            platform = ML / "platform" / camera
            source = base / f"buttons-{camera}.c"
            libname = base / f"buttons-{camera}.so"
            source.write_text(
                '#include "gui.h"\n' + common + preface + actual_translator
                + "\nint translate(int key) { return module_translate_key(key, MODULE_KEY_PORTABLE); }\n"
            )
            subprocess.run(
                ["gcc", "-std=c99", "-O2", "-shared", "-fPIC", "-Wall",
                 "-Wextra", "-Werror", "-o", str(libname), str(source),
                 f"-I{platform}"],
                check=True,
            )
            lib = ctypes.CDLL(str(libname))
            lib.translate.argtypes = [ctypes.c_int]
            lib.translate.restype = ctypes.c_int
            for raw, portable in expected.items():
                assert lib.translate(raw) == portable, (
                    f"{camera}: key 0x{raw:x} became {lib.translate(raw)}, expected {portable}"
                )
            if camera in ("700D.115", "650D.104", "100D.101", "70D.112"):
                # Touch events may be reported, but must not be treated as Q,
                # SET or focus-related button presses.
                touches = (0x6B, 0x6C, 0x72, 0x73) if camera in ("700D.115", "650D.104") else (0x6F, 0x70, 0x76, 0x77)
                for raw in touches:
                    assert lib.translate(raw) not in (9, 23, 29)
            if camera == "100D.101":
                # The combined Q/SET hardware event itself is not mapped to
                # the portable Q key: ML uses the Av button instead.
                assert lib.translate(0x1D) != 29
