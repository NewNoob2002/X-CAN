"""Bounded control-transfer regression for the migrated MS OS callback."""
import json
import usb.core
import usb.util

serial = "2036365058315010002D0055"
devices = [d for d in usb.core.find(find_all=True, idVendor=0xc0ca, idProduct=0x0313)
           if d.serial_number == serial]
assert len(devices) == 1, "Expected exactly one target serial"
dev = devices[0]

def request(length, index=7, value=0):
    return bytes(dev.ctrl_transfer(0xc0, 2, value, index, length, timeout=2000))

try:
    full = request(162)
    assert len(full) == 162
    assert full[:10] == bytes.fromhex("0a0000000000000aa200")
    assert full[10:30] == bytes.fromhex("1400030057494e55534200000000000000000000")
    assert full[30:40] == bytes.fromhex("8400040007002a004400")
    assert full[38:80].decode("utf-16le") == "DeviceInterfaceGUIDs" + chr(0)
    assert int.from_bytes(full[80:82], "little") == 80
    assert full[82:].decode("utf-16le") == "{D9E71DA8-3CB5-4E3B-8A53-5843414E0001}" + chr(0) * 2
    for length in (1, 10, 64, 255):
        assert request(length) == full[:length], f"wLength={length}"
    rejected = 0
    for index, value in ((8, 0), (7, 1)):
        try:
            request(162, index=index, value=value)
        except usb.core.USBError as error:
            assert error.backend_error_code == -9, f"Expected PIPE/STALL, got {error}"
            rejected += 1
        else:
            raise AssertionError("Invalid request was accepted")
        assert request(162) == full, "Control endpoint did not recover after STALL"
    print(json.dumps({"result": "PASS", "serial": serial, "descriptor_hex": full.hex(),
                      "lengths": [162, 1, 10, 64, 255], "invalid_requests_stalled": rejected,
                      "recovery_reads": 2}))
finally:
    usb.util.dispose_resources(dev)
