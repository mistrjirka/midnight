#!/usr/bin/env python3
"""Drive existing Canon 600D QEMU monitor, optionally capture an actual framebuffer.
QEMU must be running with -monitor unix:<socket>,server=on,wait=off.
"""
import argparse
from pathlib import Path
import socket
import time

def send(sock, command, delay):
    sock.sendall((command + "\n").encode("ascii"))
    time.sleep(delay)
    chunks = []
    sock.setblocking(False)
    while True:
        try:
            data = sock.recv(16384)
        except BlockingIOError:
            break
        if not data:
            break
        chunks.append(data)
    sock.setblocking(True)
    return b"".join(chunks).decode("latin1", errors="replace")

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--monitor", default=".astro-data/qemu-run/monitor")
    p.add_argument("--keys", nargs="*", default=[],
                   help="QEMU HMP key names, e.g. delete down space q")
    p.add_argument("--delay", type=float, default=0.6,
                   help="Delay after each key event")
    p.add_argument("--screenshot", type=Path, help="Output PNG")
    a = p.parse_args()
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(5)
        sock.connect(a.monitor)
        try:
            sock.recv(4096)
        except socket.timeout:
            pass
        for key in a.keys:
            output = send(sock, "sendkey " + key, a.delay)
            if "Error" in output or "error" in output:
                print("KEY ERROR", key, output[-400:])
            else:
                print("Key", key)
        if a.screenshot:
            ppm = a.screenshot.with_suffix(".ppm").resolve()
            ppm.parent.mkdir(parents=True, exist_ok=True)
            output = send(sock, "screendump " + str(ppm), max(a.delay, 1.0))
            if not ppm.is_file():
                raise RuntimeError("QEMU screendump failed: " + output[-500:])
            from PIL import Image
            with Image.open(ppm) as im:
                im.convert("RGB").save(a.screenshot)
                print(f"Captured {a.screenshot}: {im.size}")
            ppm.unlink()

if __name__ == "__main__":
    main()
