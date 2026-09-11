#!/usr/bin/env python3
"""Run a reviewed Commander script with an optional concurrent UART capture."""
import argparse
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument("script", type=Path)
p.add_argument("log", type=Path)
p.add_argument("--uart", type=Path)
a = p.parse_args()
capture = None
if a.uart:
    capture = subprocess.Popen([sys.executable, "tools/capture_serial.py", str(a.uart), "--seconds", "20"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    ready = capture.stdout.readline()
    print(ready, end="", flush=True)
    if "capture ready" not in ready:
        capture.wait(timeout=5)
        raise SystemExit("UART open failed; target command not executed")
try:
    result = subprocess.run(["/opt/SEGGER/JLink_V970/JLinkExe", "-Device", "STM32G431RB",
        "-If", "SWD", "-Speed", "1000", "-SelectEmuBySN", "63728769", "-ExitOnError", "1",
        "-CommanderScript", str(a.script)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, timeout=30)
    a.log.write_text(result.stdout)
    print(result.stdout, flush=True)
    result.check_returncode()
finally:
    if capture:
        output, _ = capture.communicate(timeout=25)
        print(output, end="", flush=True)
        if capture.returncode: raise SystemExit("UART capture failed")
