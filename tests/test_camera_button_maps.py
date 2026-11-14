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
            if camera == "700D.115":
                # Touch events may be reported, but must not be treated as Q,
                # SET or focus-related button presses.
                for raw in (0x6B, 0x6C, 0x72, 0x73):
                    assert lib.translate(raw) not in (9, 23, 29)
