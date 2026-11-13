"""`astroml qemu ...`: build qemu-eos, prepare a virtual SD card and drive the emulated 6D."""
from __future__ import annotations

import lzma
import pathlib
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import zipfile

import typer

from . import paths

app = typer.Typer(help="Emulate the Canon 6D with qemu-eos.", no_args_is_help=True)

RUN_DIR = paths.BUILD / "qemu-run"
CAMERA_BUILD = paths.ML_TREE / "platform" / paths.CAMERA / "build"
BASE_SD_IMAGE = paths.ML_TREE / "platform" / "sd.qcow2.xz"
MONITOR = RUN_DIR / "monitor"
ROM0_SIZE_6D = 8 << 20  # qemu-eos model_list.c: 6D rom0_size

# Optional network/storage backends are disabled: the camera needs none of them, and QEMU
# 4.2.1 no longer compiles against current libnfs and friends.
CONFIGURE = ["--target-list=arm-softmmu", "--enable-plugins", "--disable-docs", "--enable-vnc",
             "--enable-gtk", "--disable-vte", "--disable-werror", "--disable-libnfs",
             "--disable-libiscsi", "--disable-glusterfs", "--disable-libssh", "--disable-curl",
             "--disable-rbd", "--disable-smartcard", "--disable-usb-redir", "--disable-spice",
             "--disable-xen", "--disable-nettle", "--disable-gnutls", "--disable-gcrypt"]
DATA_FILES = ("STARS.BIN", "STARS.IDX", "STARS.NAM", "SKY.DIR", "SKY.PAT", "SKY.FLD",
              "DSO.BIN", "DSO.KEY", "DSO.NAM", "CONST.BIN")


def run(cmd: list[str], cwd: pathlib.Path | None = None) -> None:
    typer.echo("$ " + " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


@app.command()
def setup(jobs: int = typer.Option(0, help="make -j (0: all CPUs).")):
    """Clone qemu-eos (qemu-eos-v4.2.1) and build it into .build/qemu-eos-build."""
    import os
    jobs = jobs or os.cpu_count() or 4
    if not paths.QEMU_SOURCE.exists():
        run(["git", "clone", "--branch", paths.QEMU_BRANCH, "--depth", "1",
             paths.QEMU_REPO_URL, str(paths.QEMU_SOURCE)])
    paths.QEMU_BUILD.mkdir(parents=True, exist_ok=True)
    run([str(paths.QEMU_SOURCE / "configure"), *CONFIGURE], cwd=paths.QEMU_BUILD)
    run(["make", f"-j{jobs}"], cwd=paths.QEMU_BUILD)
    run(["make", "plugins"], cwd=paths.QEMU_BUILD)
    plugins = paths.QEMU_BUILD / "arm-softmmu" / "plugins"
    plugins.mkdir(exist_ok=True)
    shutil.copy2(paths.QEMU_BUILD / "tests" / "plugin" / "libmagiclantern.so", plugins)
    typer.echo(f"qemu-eos ready: {paths.QEMU_BUILD / 'arm-softmmu' / 'qemu-system-arm'}")


def fat_offset(raw: pathlib.Path) -> int:
    """Byte offset of the first MBR partition (the card's FAT volume)."""
    with raw.open("rb") as f:
        mbr = f.read(512)
    if mbr[510:512] != b"\x55\xaa":
        raise typer.BadParameter(f"{raw}: no MBR signature")
    (start_lba,) = struct.unpack_from("<I", mbr, 446 + 8)
    return start_lba * 512


class Card:
    """mtools access to the FAT volume inside a raw disk image."""

    def __init__(self, raw: pathlib.Path):
        self.image = f"{raw}@@{fat_offset(raw)}"

    def mkdir(self, path: str) -> None:
        subprocess.run(["mmd", "-D", "s", "-i", self.image, f"::/{path}"],
                       check=False, capture_output=True)

    def put(self, source: pathlib.Path | bytes, path: str) -> None:
        parent = path.rsplit("/", 1)[0] if "/" in path else ""
        parts = parent.split("/") if parent else []
        for i in range(len(parts)):
            self.mkdir("/".join(parts[:i + 1]))
        if isinstance(source, bytes):
            with tempfile.NamedTemporaryFile() as tmp:
                tmp.write(source)
                tmp.flush()
                self.put(pathlib.Path(tmp.name), path)
            return
        subprocess.run(["mcopy", "-o", "-i", self.image, str(source), f"::/{path}"], check=True)


def install_ml_zip(card: Card, ml_zip: pathlib.Path) -> None:
    with zipfile.ZipFile(ml_zip) as z, tempfile.TemporaryDirectory() as tmp:
        z.extractall(tmp)
        for item in sorted(pathlib.Path(tmp).rglob("*")):
            if item.is_file():
                card.put(item, str(item.relative_to(tmp)))


@app.command()
def sd(ml_zip: pathlib.Path = typer.Option(CAMERA_BUILD / "magiclantern.zip",
                                           help="ML build ZIP, e.g. the official "
                                                "magiclantern.2025-06-20.6D.116.zip."),
       module: pathlib.Path = typer.Option(paths.MODULE_BUILD, help="astro.mo to install."),
       data: pathlib.Path = typer.Option(paths.DATA, help="ML/ASTRO data files."),
       extra: list[pathlib.Path] = typer.Option([], help="Files for ML/ASTRO. Default: the "
                                                        "synthetic TEST*.PGM and tests/data/*.PGM."),
       dcim: list[pathlib.Path] = typer.Option([], help="Photos for DCIM/100CANON/, where the "
                                                        "emulated 6D shoots. Default: IMG_*.JPG.")):
    """Build sd.qcow2/cf.qcow2 with ML, ASTRO.MO (enabled) and its data, for --boot runs."""
    test_frames = sorted(data.glob("TEST*.PGM")) + sorted((paths.REPO / "tests/data").glob("*.PGM"))
    extra = extra or test_frames
    dcim = dcim or sorted(data.glob("IMG_*.JPG"))
    if not BASE_SD_IMAGE.exists():
        raise typer.BadParameter(f"{BASE_SD_IMAGE} missing: clone ML first (scripts/setup_env.sh)")
    if not ml_zip.is_file():
        raise typer.BadParameter(f"{ml_zip} missing: build ML or pass --ml-zip")
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    qcow = RUN_DIR / "base.qcow2"
    raw = RUN_DIR / "sd.raw"
    qcow.write_bytes(lzma.open(BASE_SD_IMAGE).read())
    run(["qemu-img", "convert", "-f", "qcow2", "-O", "raw", str(qcow), str(raw)])

    card = Card(raw)
    install_ml_zip(card, ml_zip)
    if module.is_file():
        card.put(module, "ML/MODULES/ASTRO.MO")
        card.put(b"", "ML/SETTINGS/ASTRO.EN")  # ML loads a module only if this file exists
    else:
        typer.echo(f"warning: {module} missing, card has stock ML only")
    for name in DATA_FILES:
        if (data / name).is_file():
            card.put(data / name, f"ML/ASTRO/{name}")
    for item in extra:
        card.put(item, f"ML/ASTRO/{item.name.upper()}")
    for photo in dcim:
        card.put(photo, f"DCIM/100CANON/{photo.name.upper()}")

    CAMERA_BUILD.mkdir(parents=True, exist_ok=True)
    sd_image = CAMERA_BUILD / "sd.qcow2"
    run(["qemu-img", "convert", "-f", "raw", "-O", "qcow2", str(raw), str(sd_image)])
    shutil.copy2(sd_image, CAMERA_BUILD / "cf.qcow2")  # the 6D model also expects a CF drive
    raw.unlink()
    qcow.unlink()
    typer.echo(f"Card image ready: {sd_image}")


def rom_dir(trim_rom0: bool) -> pathlib.Path:
    """The 6D ROM0 dump is a 16 MiB mirror of the 8 MiB flash; qemu-eos may want 8 MiB."""
    source = paths.ROMS / "6D"
    if not trim_rom0:
        return paths.ROMS
    target = paths.BUILD / "roms" / "6D"
    target.mkdir(parents=True, exist_ok=True)
    rom0 = (source / "ROM0.BIN").read_bytes()
    if len(rom0) == 2 * ROM0_SIZE_6D and rom0[:ROM0_SIZE_6D] != rom0[ROM0_SIZE_6D:]:
        raise typer.BadParameter("ROM0 halves differ: not a mirrored dump, refusing to trim")
    (target / "ROM0.BIN").write_bytes(rom0[:ROM0_SIZE_6D])
    rom1 = target / "ROM1.BIN"
    if not rom1.exists():
        rom1.symlink_to(source / "ROM1.BIN")
    return target.parent


def debugmsg_address() -> str | None:
    """DryosDebugMsg address from the 6D stubs, for the qemu-eos logging plugin."""
    stubs = paths.ML_TREE / "platform" / paths.CAMERA / "stubs.S"
    for line in stubs.read_text().splitlines():
        if "DryosDebugMsg" in line and line.strip().startswith("NSTUB("):
            return line.split("(", 1)[1].split(",", 1)[0].strip()
    return None


@app.command("run")
def run_camera(display: str = typer.Option("gtk", help="gtk (a window) or none (headless)."),
               boot: bool = typer.Option(True, help="Boot autoexec.bin from the card."),
               trim_rom0: bool = typer.Option(False, help="Use an 8 MiB copy of ROM0."),
               gdb: bool = typer.Option(False, help="Wait for gdb on port 1234."),
               debugmsg: bool = typer.Option(True, help="Log DryosDebugMsg to the serial log.")):
    """Start the emulated 6D. Monitor socket and serial log are in .build/qemu-run/."""
    import os
    qemu = paths.QEMU_BUILD / "arm-softmmu" / "qemu-system-arm"
    sd_image = CAMERA_BUILD / "sd.qcow2"
    if not qemu.exists():
        raise typer.BadParameter("qemu-eos missing: run `astroml qemu setup`")
    if not sd_image.exists():
        raise typer.BadParameter("card image missing: run `astroml qemu sd`")
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    monitor = RUN_DIR / "monitor"
    monitor.unlink(missing_ok=True)
    cmd = [str(qemu), "-drive", f"if=sd,file={sd_image}",
           "-drive", f"if=ide,file={CAMERA_BUILD / 'cf.qcow2'}",
           "-chardev", f"socket,server,nowait,path={monitor},id=monsock",
           "-mon", "chardev=monsock,mode=readline", "-name", "6D",
           "-M", f"6D,firmware=boot={1 if boot else 0}", "-display", display,
           "-serial", f"file:{RUN_DIR / 'serial.log'}"]
    if gdb:
        cmd += ["-S", "-gdb", "tcp::1234"]
    address = debugmsg_address() if debugmsg else None
    if address:
        plugin = paths.QEMU_BUILD / "arm-softmmu" / "plugins" / "libmagiclantern.so"
        cmd += ["-plugin", f"{plugin},arg=debugmsg_addr={address}", "-d", "debugmsg"]
    env = dict(os.environ, QEMU_EOS_WORKDIR=str(rom_dir(trim_rom0)))
    typer.echo(f"QEMU_EOS_WORKDIR={env['QEMU_EOS_WORKDIR']} " + " ".join(cmd))
    with (RUN_DIR / "qemu.log").open("w") as log:
        subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, check=False)


def monitor_command(sock: socket.socket, command: str, delay: float) -> str:
    sock.sendall((command + "\n").encode("ascii"))
    time.sleep(delay)
    sock.setblocking(False)
    chunks = []
    try:
        while data := sock.recv(16384):
            chunks.append(data)
    except BlockingIOError:
        pass
    sock.setblocking(True)
    return b"".join(chunks).decode("latin1", errors="replace")


@app.command()
def keys(names: list[str] = typer.Argument(None, help="QEMU key names: delete up down "
                                                       "left right ret q spc ..."),
         screenshot: pathlib.Path = typer.Option(None, help="Save the LCD as PNG afterwards."),
         delay: float = typer.Option(0.6, help="Seconds after each key."),
         monitor: pathlib.Path = typer.Option(MONITOR, help="QEMU monitor socket.")):
    """Press camera buttons and capture the emulated LCD."""
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(5)
        sock.connect(str(monitor))
        try:
            sock.recv(4096)  # banner
        except socket.timeout:
            pass
        for key in names or []:
            reply = monitor_command(sock, "sendkey " + key, delay)
            typer.echo(f"key {key}" + (f": {reply.strip()[-200:]}" if "rror" in reply else ""))
        if screenshot:
            from PIL import Image
            ppm = screenshot.with_suffix(".ppm").resolve()
            ppm.parent.mkdir(parents=True, exist_ok=True)
            monitor_command(sock, f"screendump {ppm}", max(delay, 1.0))
            with Image.open(ppm) as image:
                image.convert("RGB").save(screenshot)
            ppm.unlink()
            typer.echo(f"saved {screenshot}")
