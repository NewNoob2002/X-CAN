#!/usr/bin/env python3
"""Bounded UART capture, receive only; target TX and ground are the only wires."""
import argparse
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("--seconds", type=int, default=20, choices=range(1, 61))
args = parser.parse_args()
port = serial.Serial(port=None, baudrate=115200, timeout=0.2, exclusive=True)
port.dtr = False
port.rts = False
port.port = "/dev/ttyUSB0"
port.open()
print("UART capture ready: ttyUSB0 115200 8N1", flush=True)
try:
    deadline = time.monotonic() + args.seconds
    with args.output.open("wb") as log:
        while time.monotonic() < deadline:
            data = port.read(4096)
            if data:
                log.write(data)
                log.flush()
                print(data.decode("utf-8", errors="backslashreplace"), end="", flush=True)
finally:
    port.close()
