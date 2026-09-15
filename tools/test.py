#!/usr/bin/env python3
"""Local protocol/USB regression only; never opens hardware."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / "build/host-tests"
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")  # ptrace sandbox lacks LeakSanitizer
common = ["cc", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter", "-fsanitize=address,undefined",
          "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-Ifirmware/src",
          "-Ifirmware/boot/include", "-Icomponents/MCUboot/boot/bootutil/include",
          "-Icomponents/MCUboot/ext/tinycrypt/lib/include",
          "-Icomponents/CherryUSB/core", "-Icomponents/CherryUSB/common"]
suites = [
    ("protocol", ["tests/protocol_test.c", "firmware/src/protocol.c"], []),
    ("protocol_v2", ["tests/protocol_v2_test.c", "firmware/src/protocol_v2.c"], ["tests/protocol_v2_vectors.txt"]),
    ("fw_update", ["tests/fw_update_test.c", "firmware/src/fw_update.c", "components/MCUboot/ext/tinycrypt/lib/source/sha256.c", "components/MCUboot/ext/tinycrypt/lib/source/utils.c"], []),
    ("usb", ["tests/usb_test.c", "firmware/src/usb.c", "firmware/src/protocol.c", "firmware/src/protocol_v2.c", "components/CherryUSB/core/usbd_core.c"], []),
]
for name, sources, args in suites:
    binary = out / name
    subprocess.run(common + sources + ["-o", str(binary)], cwd=root, check=True)
    subprocess.run([str(binary)] + args, cwd=root, env=env, check=True)
    print(f"PASS {name} ASan/UBSan", flush=True)
subprocess.run(["cargo", "test", "--locked", "--manifest-path", "host/Cargo.toml"], cwd=root, check=True)
