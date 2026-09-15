#!/usr/bin/env python3
"""Build the repository-owned CubeMX/HAL + FreeRTOS application. No downloads."""
import argparse
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--preset", choices=("Debug", "Release"), default="Release")
args = parser.parse_args()
subprocess.run(["cmake", "--preset", args.preset], cwd=root, check=True)
subprocess.run(["cmake", "--build", "--preset", args.preset, "--parallel", "8"], cwd=root, check=True)
subprocess.run([sys.executable, "tools/check_build.py", "build/" + args.preset], cwd=root, check=True)
