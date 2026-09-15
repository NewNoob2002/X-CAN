/* SPDX-License-Identifier: Apache-2.0 */
#ifndef XCAN_PROTOCOL_V2_H
#define XCAN_PROTOCOL_V2_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XCAN_V2_HEADER 24u
#define XCAN_V2_MAX_MESSAGE 544u
#define XCAN_V2_MAX_PAYLOAD (XCAN_V2_MAX_MESSAGE - XCAN_V2_HEADER)
#define XCAN_V2_REQUEST 1u
#define XCAN_V2_RESPONSE 2u
#define XCAN_V2_EVENT 3u
#define XCAN_V2_HELLO 1u
#define XCAN_V2_STATUS 2u
#define XCAN_V2_CONFIGURE 0x10u
#define XCAN_V2_START 0x11u
#define XCAN_V2_STOP 0x12u
#define XCAN_V2_SEND 0x13u
#define XCAN_V2_FW_BEGIN 0x20u
#define XCAN_V2_FW_STATUS 0x21u
#define XCAN_V2_FW_WRITE 0x22u
#define XCAN_V2_FW_FINISH 0x23u
#define XCAN_V2_FW_ABORT 0x24u
#define XCAN_V2_FW_REBOOT 0x25u
#define XCAN_V2_FRAMES 0x8001u
#define XCAN_V2_STATE 0x8002u
#define XCAN_V2_TX_RESULT 0x8003u

struct xcan_v2_message {
    uint8_t kind;
    uint16_t opcode, status, length;
    uint32_t id, session, sequence;
    uint8_t payload[XCAN_V2_MAX_PAYLOAD];
};

/* Caller owns storage. No packed structs, allocation or hardware access. */
struct xcan_v2_decoder {
    uint8_t bytes[XCAN_V2_MAX_MESSAGE];
    size_t used;
    bool failed;
};

struct xcan_v2_can_frame {
    uint16_t flags;
    uint32_t id, sequence;
    uint64_t timestamp_us;
    uint8_t dlc, length, data[64];
};
size_t xcan_v2_frame_encode(const struct xcan_v2_can_frame *frame, bool tx,
                            uint8_t *wire, size_t capacity);
/* Returns the consumed record size, or 0; permits more records after this one. */
size_t xcan_v2_frame_decode(const uint8_t *wire, size_t length, bool tx,
                            struct xcan_v2_can_frame *frame);

bool xcan_v2_valid(const struct xcan_v2_message *message);
/* Encode returns wire length, or 0 on invalid input / insufficient capacity. */
size_t xcan_v2_encode(const struct xcan_v2_message *message, uint8_t *wire, size_t capacity);
bool xcan_v2_decode(const uint8_t *wire, size_t length, struct xcan_v2_message *message);
/* One message per call: 1=ready, 0=partial, -1=latched failure.
 * Always advance by consumed; pass remaining input again for coalesced messages.
 * Zero-initialize once per new USB session; never reset just to skip bad bytes. */
int xcan_v2_feed(struct xcan_v2_decoder *decoder, const uint8_t *data, size_t length,
                 size_t *consumed, struct xcan_v2_message *message);
/* Call at EOF/disconnect. Truncation latches failure. */
bool xcan_v2_finish(struct xcan_v2_decoder *decoder);
#endif
