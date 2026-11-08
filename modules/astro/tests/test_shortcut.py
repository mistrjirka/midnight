#!/usr/bin/env python3
"""Verify camera-specific shortcut filtering without any camera hardware."""
import ctypes as C
import pathlib
import subprocess

ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/"build"/"libastro_shortcut_test.so"
OUT.parent.mkdir(exist_ok=True)
subprocess.run(["gcc","-std=c99","-Wall","-Wextra","-Werror","-fPIC","-shared",
                "-o",str(OUT),str(ROOT/"astro_shortcut.c")],check=True)
f=C.CDLL(str(OUT)).astro_shortcut_decide
f.argtypes=[C.c_int]*7
f.restype=C.c_int
cases=[
    ("600D DISP AF",600,8,1,1,0,0,1,1),
    ("600D DISP MF",600,8,1,1,0,0,0,2),
    ("600D 6D key rejected",600,0x20,1,1,0,0,1,0),
    ("6D LCD-light AF",6,0x20,1,1,0,0,1,1),
    ("6D LCD-light MF",6,0x20,1,1,0,0,0,2),
    ("6D DISP rejected",6,8,1,1,0,0,1,0),
    ("600D disabled",600,8,0,1,0,0,1,0),
    ("6D disabled",6,0x20,0,1,0,0,1,0),
    ("not LV",600,8,1,0,0,0,1,0),
    ("menu open",600,8,1,1,1,0,1,0),
    ("busy",600,8,1,1,0,1,1,0),
    ("unrecognized",0,8,1,1,0,0,1,0),
    ("no keypress",600,99,1,1,0,0,1,0),
]
for name,*case in cases:
    got=f(*case[:-1])
    assert got==case[-1],(name,got,case[-1])
    print(name+": PASS")
print("PASS: %d shortcut scenarios" % len(cases))
