#!/usr/bin/env python3
"""Validate X-CAN partitions and create the factory image metadata."""

import argparse
import hashlib
import json
from pathlib import Path

BOOT_SIZE = 0x6000
PRIMARY_BASE = 0x08006000
PRIMARY_SIZE = 0xC800
SECONDARY_BASE = 0x08012800
SECONDARY_SIZE = 0xD000
SECONDARY_IMAGE_BASE = 0x08013000
PRODUCT_DATA_BASE = 0x0801F800
SIGNED_IMAGE_MAX = 0xC000
APP_LINK_BASE = 0x08006200


def digest(path):
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


parser = argparse.ArgumentParser()
parser.add_argument("--boot", type=Path, required=True)
parser.add_argument("--app", type=Path, required=True)
parser.add_argument("--signed", type=Path, required=True)
parser.add_argument("--factory", type=Path, required=True)
parser.add_argument("--metadata", type=Path, required=True)
parser.add_argument("--version", required=True)
parser.add_argument("--key-usage", required=True)
args = parser.parse_args()

boot = args.boot.read_bytes()
app = args.app.read_bytes()
signed = args.signed.read_bytes()
if len(boot) > BOOT_SIZE:
    raise SystemExit(f"xcan_boot exceeds 24 KiB: {len(boot)} > {BOOT_SIZE}")
if len(signed) > SIGNED_IMAGE_MAX:
    raise SystemExit(
        f"signed application exceeds 48 KiB primary capacity: "
        f"{len(signed)} > {SIGNED_IMAGE_MAX}"
    )
if len(signed) < 0x200 or signed[0x200 : 0x200 + min(16, len(app))] != app[:16]:
    raise SystemExit("signed image does not contain the application at header offset 0x200")

args.factory.write_bytes(boot + b"\xff" * (BOOT_SIZE - len(boot)) + signed)
metadata = {
    "format": 1,
    "image_version": args.version,
    "signature": {"algorithm": "ECDSA-P256", "key_usage": args.key_usage},
    "factory_programming": {
        "address": "0x08000000",
        "chip_erase_required": True,
        "reason": "compact factory image does not clear stale secondary/trailer state",
    },
    "layout": {
        "boot": {"address": "0x08000000", "size": BOOT_SIZE},
        "primary": {"address": f"0x{PRIMARY_BASE:08X}", "size": PRIMARY_SIZE},
        "app_link": {"address": f"0x{APP_LINK_BASE:08X}"},
        "secondary": {"address": f"0x{SECONDARY_BASE:08X}", "size": SECONDARY_SIZE},
        "secondary_image_write": {"address": f"0x{SECONDARY_IMAGE_BASE:08X}"},
        "product_data": {"address": f"0x{PRODUCT_DATA_BASE:08X}", "size": 0x800},
        "signed_image_max": SIGNED_IMAGE_MAX,
    },
    "artifacts": {},
}
for path in (args.boot, args.app, args.signed, args.factory):
    metadata["artifacts"][path.name] = digest(path)
args.metadata.write_text(json.dumps(metadata, indent=2) + "\n")
print(
    f"PASS package boot={len(boot)} signed={len(signed)} "
    f"headroom={SIGNED_IMAGE_MAX - len(signed)} factory={args.factory.stat().st_size}"
)
