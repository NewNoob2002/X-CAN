#!/usr/bin/env python3
"""Generate a reviewed project-local M_CAN variant; never modify Zephyr."""
import hashlib
from pathlib import Path
import sys

source, target = map(Path, sys.argv[1:])
raw = source.read_bytes()
if hashlib.sha256(raw).hexdigest() != "1cfdb5db97255afd0c64d554770705b63bfcccb7c07aff8f09702f8f9c19bb97":
    raise SystemExit("M_CAN source changed: review X-CAN fixes before building this Zephyr revision")
text = raw.decode()
changes = [
    ("*cap = CAN_MODE_NORMAL | CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY;",
     "*cap = CAN_MODE_NORMAL | CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY | CAN_MODE_ONE_SHOT;"),
    ("can_mode_t supported = CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY;",
     "can_mode_t supported = CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY | CAN_MODE_ONE_SHOT;"),
    ("\tif ((mode & CAN_MODE_LOOPBACK) != 0) {",
     "\t/* X-CAN: explicit single attempt, ES0431 edge filtering disabled. */\n"
     "\tcccr &= ~(CAN_MCAN_CCCR_DAR | CAN_MCAN_CCCR_EFBI);\n"
     "\tif ((mode & CAN_MODE_ONE_SHOT) != 0) { cccr |= CAN_MCAN_CCCR_DAR; }\n\n"
     "\tif ((mode & CAN_MODE_LOOPBACK) != 0) {"),
    ("\twhile (FIELD_GET(CAN_MCAN_RXF0S_F0FL, fifo_status) != 0U) {",
     "\twhile (FIELD_GET(CAN_MCAN_RXF0S_F0FL, fifo_status) != 0U) {\n"
     "\t\t/* Flags must not leak from an earlier FIFO record. */\n"
     "\t\tframe = (struct can_frame){0};"),
    ("\t\tk_sem_give(&data->tx_sem);\n\n\t\ttx_cb(dev, 0, user_data);",
     "\t\t/* Stop may already have retired this callback. */\n"
     "\t\tif (tx_cb != NULL) {\n\t\t\tk_sem_give(&data->tx_sem);\n"
     "\t\t\ttx_cb(dev, 0, user_data);\n\t\t}"),
]
for old, new in changes:
    if text.count(old) != 1:
        raise SystemExit("M_CAN patch context is not unique")
    text = text.replace(old, new)
target.write_text(text)
