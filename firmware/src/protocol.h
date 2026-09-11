/* SPDX-License-Identifier: Apache-2.0 */
#ifndef XCAN_PROTOCOL_H
#define XCAN_PROTOCOL_H
#include <stdint.h>
#define XCAN_FRAME_SIZE 64
#define XCAN_PAYLOAD_SIZE 52
/* Fixed 64-byte records: XCAN, version, opcode, status, length, LE32 id, payload. */
void xcan_reply(const uint8_t request[64], uint8_t response[64],
                uint32_t clock_hz, uint32_t idcode, uint32_t uptime_ms, int term);
#endif
