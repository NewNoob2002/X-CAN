#!/usr/bin/env python3
"""Validate relocated App, MCUboot, package layout, and build ownership."""

import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

BOOT_SIZE = 0x6000
APP_VECTOR_BASE = 0x08006200
APP_LINK_SIZE = 0xBE00
SIGNED_IMAGE_MAX = 0xC000
MCUBOOT_MAGIC = 0x96F3B83D

build = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parents[1]
commands = json.loads((build / "compile_commands.json").read_text())
compiler = shlex.split(commands[0]["command"])[0]
prefix = compiler.removesuffix("gcc")
app_elf = build / "xcan.elf"
boot_elf = build / "xcan_boot.elf"


def dump(elf, symbol):
    return subprocess.check_output(
        [prefix + "objdump", "-d", "--disassemble=" + symbol, str(elf)], text=True
    )


def instructions(text):
    result = []
    for line in text.splitlines():
        left, sep, right = line.partition(":")
        if sep and left.strip() and all(c in "0123456789abcdef" for c in left.strip()):
            result.append(right)
    return result


def symbols(elf):
    result = {}
    output = subprocess.check_output([prefix + "nm", "-n", str(elf)], text=True)
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3:
            result[parts[2]] = int(parts[0], 16)
    return result


def ram_usage(elf):
    lines = subprocess.check_output([prefix + "size", str(elf)], text=True).splitlines()
    fields = lines[1].split()
    return int(fields[1]) + int(fields[2])


def cmake_cache_value(name):
    prefix_text = name + ":"
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line.startswith(prefix_text):
            return line.split("=", 1)[1]
    raise AssertionError(f"missing CMake cache value {name}")


app_symbols = symbols(app_elf)
boot_symbols = symbols(boot_elf)
app_binary = (build / "xcan.bin").read_bytes()
boot_binary = (build / "xcan_boot.bin").read_bytes()
signed_binary = (build / "xcan.signed.bin").read_bytes()
factory_binary = (build / "xcan_factory.bin").read_bytes()
package_metadata = json.loads((build / "firmware-metadata.json").read_text())

start = dump(app_elf, "Reset_Handler")
hook = dump(app_elf, "soc_early_reset_hook")
startup = instructions(start)
hook_index = next(i for i, line in enumerate(startup) if "<soc_early_reset_hook>" in line)
system_index = next(i for i, line in enumerate(startup) if "<SystemInit>" in line)
assert hook_index < system_index, "SRAM hook must run before SystemInit"
for line in startup[:hook_index]:
    words = line.split()
    assert not any(w.startswith(("str", "stm", "push")) for w in words), "SRAM write before hook"
for line in instructions(hook):
    words = line.split()
    assert not any(w.startswith(("str", "stm", "push")) for w in words), "SRAM write in hook"
assert "xcan_sram_parity_unsupported" in hook

app_vectors = struct.unpack_from("<118I", app_binary)
assert app_symbols["g_pfnVectors"] == APP_VECTOR_BASE
assert app_vectors[0] == 0x20008000
for index, name in [
    (1, "Reset_Handler"),
    (11, "SVC_Handler"),
    (14, "PendSV_Handler"),
    (15, "SysTick_Handler"),
    (36, "USB_LP_IRQHandler"),
    (37, "FDCAN1_IT0_IRQHandler"),
    (70, "TIM6_DAC_IRQHandler"),
]:
    assert app_vectors[index] == app_symbols[name] | 1, name
assert len(app_binary) <= APP_LINK_SIZE
assert "xPortStartScheduler" in app_symbols and "HAL_InitTick" in app_symbols
assert "boot_set_confirmed_multi" in app_symbols
assert {"xcan_fw_begin", "xcan_fw_finish", "xcan_v2_decode", "xcan_reboot"} <= app_symbols.keys()
assert {"HAL_FDCAN_Init", "HAL_FDCAN_IRQHandler"} <= app_symbols.keys()
assert not {"HAL_PCD_IRQHandler", "HAL_PCD_Init", "HAL_FDCAN_Start"} & app_symbols.keys()
main = dump(app_elf, "main")
assert main.index("<xcan_board_safe>") < main.index("<HAL_Init>")
assert "<xcan_start>" in main and "<xcan_board_init>" in main
assert "<HAL_TIM_IRQHandler>" in dump(app_elf, "TIM6_DAC_IRQHandler")
assert "<USBD_IRQHandler>" in dump(app_elf, "USB_LP_IRQHandler")
assert "<HAL_IncTick>" not in dump(app_elf, "SysTick_Handler")
assert "<vPortSVCHandler>" in dump(app_elf, "SVC_Handler")
assert "<xPortPendSVHandler>" in dump(app_elf, "PendSV_Handler")

boot_vectors = struct.unpack_from("<2I", boot_binary)
assert boot_symbols["g_pfnVectors"] == 0x08000000
assert boot_vectors[0] == 0x20008000
assert boot_vectors[1] == boot_symbols["Reset_Handler"] | 1
assert {"boot_go", "HAL_FLASH_Program", "HAL_FLASHEx_Erase"} <= boot_symbols.keys()
assert not {
    "vTaskStartScheduler",
    "USBD_Init",
    "USBD_IRQHandler",
    "HAL_FDCAN_Init",
    "HAL_FDCAN_IRQHandler",
} & boot_symbols.keys()
assert len(boot_binary) <= BOOT_SIZE

assert struct.unpack_from("<I", signed_binary)[0] == MCUBOOT_MAGIC
assert signed_binary[0x200 : 0x200 + len(app_binary)] == app_binary
assert len(signed_binary) <= SIGNED_IMAGE_MAX
assert factory_binary[: len(boot_binary)] == boot_binary
assert set(factory_binary[len(boot_binary) : BOOT_SIZE]) <= {0xFF}
assert factory_binary[BOOT_SIZE:] == signed_binary
assert package_metadata["layout"]["app_link"]["address"] == "0x08006200"
assert package_metadata["layout"]["secondary_image_write"]["address"] == "0x08013000"
assert package_metadata["factory_programming"]["chip_erase_required"] is True

imgtool = root / "components/MCUboot/scripts/imgtool.py"
pythonpath = os.pathsep.join([str(imgtool.parent), str(imgtool.parent / "vendor")])
signing_key = cmake_cache_value("XCAN_SIGNING_KEY")
verify = [sys.executable, str(imgtool), "verify", "--key", signing_key]
env = dict(os.environ, PYTHONPATH=pythonpath)
subprocess.run(
    verify + [str(build / "xcan.signed.bin")],
    env=env,
    check=True,
    stdout=subprocess.DEVNULL,
)
with tempfile.TemporaryDirectory() as temporary:
    damaged = bytearray(signed_binary)
    damaged[0x220] ^= 1
    damaged_path = Path(temporary) / "damaged.bin"
    damaged_path.write_bytes(damaged)
    invalid = subprocess.run(
        verify + [str(damaged_path)],
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    assert invalid.returncode != 0, "damaged signed image was accepted"
    wrong_key = ec.generate_private_key(ec.SECP256R1())
    wrong_key_path = Path(temporary) / "wrong-key.pem"
    wrong_key_path.write_bytes(
        wrong_key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )
    wrong_signature = subprocess.run(
        [sys.executable, str(imgtool), "verify", "--key", str(wrong_key_path),
         str(build / "xcan.signed.bin")],
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    assert wrong_signature.returncode != 0, "image was accepted with the wrong key"

for image_symbols in (app_symbols, boot_symbols):
    assert image_symbols["_Min_Stack_Size"] == 1024
    assert image_symbols["_Min_Heap_Size"] == 512

assert all(
    "/legacy/" not in entry["file"] and "/comparison/" not in entry["file"]
    for entry in commands
)
source_paths = {Path(entry["file"]).resolve() for entry in commands}
assert root / "startup_stm32g431xx.s" in source_paths
assert root / "Core/Src/early_reset.S" in source_paths
assert root / "Core/Src/stm32g4xx_it.c" in source_paths
assert root / "Core/Src/sysmem.c" in source_paths
assert not {
    root / "firmware/src/interrupts.c",
    root / "firmware/src/sysmem.c",
} & source_paths

app_ram = ram_usage(app_elf)
boot_ram = ram_usage(boot_elf)
assert app_ram <= 32 * 1024 and boot_ram <= 32 * 1024
metadata = {
    "scope": "CubeMX App plus MCUboot 2.4.0 offset swap; local and HIL verified",
    "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
    "app_flash_bytes": len(app_binary),
    "app_signed_bytes": len(signed_binary),
    "app_signed_headroom_bytes": SIGNED_IMAGE_MAX - len(signed_binary),
    "app_ram_reserved_bytes": app_ram,
    "boot_flash_bytes": len(boot_binary),
    "boot_flash_headroom_bytes": BOOT_SIZE - len(boot_binary),
    "boot_ram_reserved_bytes": boot_ram,
    "libc_heap_limit": app_symbols["_Min_Heap_Size"],
    "isr_stack_bytes": app_symbols["_Min_Stack_Size"],
    "freertos_dynamic_allocation": False,
    "firmware_package": package_metadata,
    "artifacts": {},
    "source_sha256": {},
}
artifact_paths = [
    build / "xcan.elf",
    build / "xcan.bin",
    build / "xcan.hex",
    build / "xcan.map",
    build / "xcan_boot.elf",
    build / "xcan_boot.bin",
    build / "xcan_boot.hex",
    build / "xcan_boot.map",
    build / "xcan.signed.bin",
    build / "xcan_factory.bin",
    build / "firmware-metadata.json",
    build / "compile_commands.json",
]
for path in artifact_paths:
    data = path.read_bytes()
    metadata["artifacts"][path.name] = {
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }
source_paths.update(root.glob("Core/Inc/*.h"))
source_paths.update(root.glob("firmware/src/*.h"))
source_paths.update(root.glob("firmware/boot/**/*.[ch]"))
source_paths.update(
    [
        root / "STM32G431XX_FLASH.ld",
        root / "CMakeLists.txt",
        root / "components/CMakeLists.txt",
        root / "firmware/CMakeLists.txt",
        root / "cmake/gcc-arm-none-eabi.cmake",
        root / "tools/package_firmware.py",
    ]
)
for path in sorted(source_paths):
    if path.is_file():
        metadata["source_sha256"][str(path.relative_to(root))] = hashlib.sha256(
            path.read_bytes()
        ).hexdigest()
(build / "build-metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
(build / "startup-disassembly.txt").write_text(start + hook)
print(
    "PASS relocated App, MCUboot vectors/signature/package, RTOS/USB/FDCAN ownership; "
    f"App={len(app_binary)} Signed={len(signed_binary)} RAM={app_ram} "
    f"Boot={len(boot_binary)} RAM={boot_ram}"
)
