#!/usr/bin/env python3
"""Build pinned local dependencies without changing an existing west workspace."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PINS = {
    "zephyr": "684c9e8f32e4373a21098559f748f06915f950c9",
    "hal_stm32": "39130f29ae37c1db34095478ca02b6419b70dcdc",
    "cmsis_6": "30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74",
}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--usb", action="store_true")
    args = parser.parse_args()
    for name, revision in PINS.items():
        path = ROOT / ".deps" / name
        actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        if actual != revision:
            raise SystemExit(f"{name}: expected {revision}, got {actual}")
        subprocess.run(["git", "-C", str(path), "diff", "--exit-code", "HEAD", "--"],
                       check=True, stdout=subprocess.DEVNULL)
    env = dict(os.environ, ZEPHYR_BASE=str(ROOT / ".deps/zephyr"))
    build = ROOT / "build" / ("usb" if args.usb else "bringup")
    cmd = ["cmake", "-S", str(ROOT / "firmware"), "-B", str(build), "-GNinja",
           "-DBOARD=xcan_g431", f"-DPython3_EXECUTABLE={sys.executable}",
           "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
           "-DZEPHYR_MODULES=" + ";".join(str(ROOT / ".deps" / m) for m in ("hal_stm32", "cmsis_6"))]
    if args.usb:
        cmd += ["-DEXTRA_CONF_FILE=usb.conf", "-DDTC_OVERLAY_FILE=usb.overlay"]
    subprocess.run(cmd, env=env, check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", "8"], env=env, check=True)

if __name__ == "__main__":
    main()
