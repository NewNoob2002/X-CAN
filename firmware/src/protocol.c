/* SPDX-License-Identifier: Apache-2.0 */
#include "protocol.h"
#include <string.h>

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned int i = 0; i < 4; ++i) { p[i] = (uint8_t)(value >> (8 * i)); }
}

void xcan_reply(const uint8_t request[64], uint8_t response[64],
                uint32_t clock_hz, uint32_t idcode, uint32_t uptime_ms, int term)
{
    memset(response, 0, 64);
    memcpy(response, "XCAN", 4);
    response[4] = 1;
    response[5] = request[5];
    memcpy(response + 8, request + 8, 4);
    if (memcmp(request, "XCAN", 4) || request[4] != 1 ||
        request[6] != 0 || request[7] > XCAN_PAYLOAD_SIZE) {
        response[6] = 1; /* Malformed header/version/length. */
        return;
    }
    if (request[5] == 1 && request[7] == 0) {
        response[7] = 14;
        put32(response + 12, clock_hz);
        put32(response + 16, idcode);
        put32(response + 20, uptime_ms);
        response[24] = term < 0 ? 0xff : (uint8_t)term;
        response[25] = 1; /* SAFE, no CAN or injection commands implemented. */
    } else if (request[5] == 2) {
        response[7] = request[7];
        memcpy(response + 12, request + 12, request[7]);
    } else {
        response[6] = 2; /* Unsupported command or arguments. */
    }
}
