"""Bounded M1 USB recovery HIL; one bus reset, no CAN commands."""
import json
from pathlib import Path
import time
import usb.core
import usb.util

SERIAL = "2036365058315010002D0055"
OUT = Path(__file__).parent

def target():
    devices = [d for d in usb.core.find(find_all=True, idVendor=0xc0ca, idProduct=0x0313)
               if d.serial_number == SERIAL]
    assert len(devices) == 1, "Expected exactly one target serial"
    return devices[0]

def request(op, transaction, data=b""):
    assert len(data) <= 52
    return b"XCAN" + bytes([1, op, 0, len(data)]) + transaction.to_bytes(4, "little") + data.ljust(52, b"\0")

def exchange(dev, frame, hold=0):
    assert dev.write(1, frame, timeout=2000) == 64
    if hold:
        time.sleep(hold)
    answer = bytes(dev.read(0x81, 64, timeout=2000))
    assert len(answer) == 64 and answer[:6] == frame[:6]
    assert answer[6] == 0 and answer[8:12] == frame[8:12]
    return answer

dev = target()
try:
    usb.util.claim_interface(dev, 0)
    frame = request(2, 201, b"host-paused-reading")
    assert exchange(dev, frame, hold=0.25) == frame
    # A new host handle must still work after the previous process releases it.
    usb.util.release_interface(dev, 0)
    usb.util.dispose_resources(dev)
    dev = target()
    usb.util.claim_interface(dev, 0)
    frame = request(2, 202, b"reopen")
    assert exchange(dev, frame) == frame
    # Abandoned partial v1 transactions require a USB reset to clear framing.
    assert dev.write(1, request(2, 203, b"abandoned")[:7], timeout=2000) == 7
    dev.reset()
    usb.util.dispose_resources(dev)
    time.sleep(1)
    dev = target()
    usb.util.claim_interface(dev, 0)
    frame = request(2, 204, b"after-reset")
    assert exchange(dev, frame) == frame
    info = exchange(dev, request(1, 205))
    assert info[7] == 14 and info[25] == 1
    assert int.from_bytes(info[12:16], "little") == 144000000
    assert int.from_bytes(info[16:20], "little") == 0x20036468
    result = {"result": "PASS", "serial": SERIAL, "host_read_pause_ms": 250,
              "reopen": True, "partial_request_reset_recovery": True, "usb_resets": 1,
              "termination_declared": info[24], "safe": info[25]}
    (OUT / "usb-recovery-result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))
finally:
    usb.util.dispose_resources(dev)
