#!/usr/bin/env python3
"""Build with an existing Zephyr workspace; never download dependencies."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--usb", action="store_true")
    parser.add_argument("--can", action="store_true", help="v2 USB/FDCAN firmware, initially stopped")
    parser.add_argument("--loopback", action="store_true", help="v2 internal CAN loopback; transceiver stays in standby")
    parser.add_argument("--zephyr-base", help="existing Zephyr source directory (default: ZEPHYR_BASE)")
    args = parser.parse_args()
    base = args.zephyr_base or os.environ.get("ZEPHYR_BASE")
    if not base:
        parser.error("set ZEPHYR_BASE or pass --zephyr-base pointing to an existing Zephyr checkout")
    base = Path(base).expanduser().resolve()
    package = base / "share/zephyr-package/cmake"
    if not (package / "ZephyrConfig.cmake").is_file():
        parser.error(f"not a Zephyr checkout: {base}")
    env = dict(os.environ, ZEPHYR_BASE=str(base))
    variant = "can-loopback" if args.loopback else "can" if args.can else "usb" if args.usb else "bringup"
    build = ROOT.parents[1] / "build/legacy-zephyr" / variant
    cmd = ["cmake", "-S", str(ROOT / "firmware"), "-B", str(build), "-GNinja",
           "-DBOARD=xcan_g431", f"-DPython3_EXECUTABLE={sys.executable}",
           "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", f"-DZephyr_DIR={package}",
           f"-DUSER_CACHE_DIR={ROOT / 'build/.cache'}"]
    if args.can or args.loopback:
        cmd += ["-DEXTRA_CONF_FILE=usb.conf;can.conf", "-DDTC_OVERLAY_FILE=usb.overlay;can.overlay"]
        if args.loopback:
            cmd += ["-DCONFIG_XCAN_INTERNAL_LOOPBACK=y"]
    elif args.usb:
        cmd += ["-DEXTRA_CONF_FILE=usb.conf", "-DDTC_OVERLAY_FILE=usb.overlay"]
    subprocess.run(cmd, env=env, check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", "8"], env=env, check=True)

if __name__ == "__main__":
    main()
