#!/usr/bin/env python3
"""Check the linked startup contract and archive image metadata."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from elftools.elf.elffile import ELFFile

build = Path(sys.argv[1]).resolve()
cache = (build / "CMakeCache.txt").read_text()
objdump = re.search(r"^CMAKE_OBJDUMP:FILEPATH=(.+)$", cache, re.M).group(1)
elf_path = build / "zephyr/zephyr.elf"
def disassemble(symbol):
    return subprocess.check_output([objdump, "-d", "--disassemble=" + symbol, str(elf_path)], text=True)
start = disassemble("__start")
hook = disassemble("soc_early_reset_hook")
instructions = [s for s in start.splitlines() if re.match(r"\s*[0-9a-f]+:\s", s)]
assert instructions and "<soc_early_reset_hook>" in instructions[0], "SRAM hook is not first"
assert not re.search(r"\b(str\w*|push|stm\w*|cpsid)\b", hook), "unexpected write/mask in hook"
assert "xcan_sram_parity_unsupported" in hook
config = (build / "zephyr/.config").read_text()
assert "CONFIG_SOC_EARLY_RESET_HOOK=y" in config
assert "CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=144000000" in config
assert "CONFIG_CAN=y" not in config
assert "CONFIG_BOOTLOADER_MCUBOOT=y" not in config
with elf_path.open("rb") as f:
    elf = ELFFile(f)
    vectors = elf.get_section_by_name("rom_start").data()
    sp = int.from_bytes(vectors[:4], "little")
    reset = int.from_bytes(vectors[4:8], "little")
    assert 0x20000000 < sp <= 0x20008000
    assert 0x08000000 <= (reset & ~1) < 0x08020000 and reset & 1
    symbols = elf.get_section_by_name(".symtab")
    assert reset == (symbols.get_symbol_by_name("__start")[0]["st_value"] | 1)
artifacts = {}
for name in ("zephyr.elf", "zephyr.bin", "zephyr.hex", "zephyr.map", ".config", "zephyr.dts"):
    p = build / "zephyr" / name
    data = p.read_bytes()
    artifacts[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
metadata = {"build": str(build), "checks": "startup hook first, no stores in hook, Flash vectors, no CAN",
            "artifacts": artifacts, "initial_sp": hex(sp), "reset_vector": hex(reset)}
(build / "build-metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
(build / "startup-disassembly.txt").write_text(start + hook)
print(json.dumps(metadata, indent=2))
