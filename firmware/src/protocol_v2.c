/* SPDX-License-Identifier: Apache-2.0 */
#include "protocol_v2.h"
#include <string.h>

static uint16_t u16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put(uint8_t *p, uint64_t value, size_t n)
{
    for (size_t i = 0; i < n; ++i) { p[i] = (uint8_t)(value >> (8 * i)); }
}
static bool zero(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; ++i) { if (p[i]) { return false; } }
    return true;
}

static bool envelope(const struct xcan_v2_message *m)
{
    if (m->length > XCAN_V2_MAX_PAYLOAD || m->kind < 1 || m->kind > 3) { return false; }
    if (m->kind == XCAN_V2_EVENT) {
        return m->opcode >= 0x8000 && m->id == 0 && m->session != 0 && m->status == 0;
    }
    if (m->opcode == 0 || m->opcode >= 0x8000 || m->id == 0 || m->sequence != 0) { return false; }
    if (m->kind == XCAN_V2_REQUEST && m->status != 0) { return false; }
    if (m->status > 6 || (m->status && m->length)) { return false; }
    if (m->opcode == XCAN_V2_HELLO) {
        return (m->kind == XCAN_V2_REQUEST || m->status) ? m->session == 0 : m->session != 0;
    }
    return m->session != 0;
}

/* CAN record = channel, flags, id, timestamp, sequence, DLC, data length, reserved, data. */
static size_t frame(const uint8_t *p, size_t n, bool tx)
{
    static const uint8_t fd_lengths[16] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    if (n < 24) { return 0; }
    uint16_t flags = u16(p + 2);
    uint32_t id = u32(p + 4);
    uint8_t dlc = p[20], len = p[21];
    bool extended = flags & 1, rtr = flags & 2, fd = flags & 4;
    if (p[0] || p[1] || (flags & ~31u) || !zero(p + 22, 2) ||
        id > (extended ? 0x1fffffffu : 0x7ffu) || dlc > 15 || len > 64 || n < 24u + len) { return 0; }
    if ((!fd && (dlc > 8 || (flags & 24))) || (fd && rtr)) { return 0; }
    if (len != (rtr ? 0 : fd_lengths[dlc])) { return 0; }
    if (tx && ((flags & 16) || !zero(p + 8, 12))) { return 0; }
    return 24u + len;
}

static bool state(const uint8_t *p, size_t n)
{
    return n == 40 && p[0] <= 4 && p[1] <= 1 &&
           (p[2] <= 1 || p[2] == 255) && p[3] == 0;
}

size_t xcan_v2_frame_encode(const struct xcan_v2_can_frame *f, bool tx,
                            uint8_t *wire, size_t capacity)
{
    if (f->length > 64 || capacity < 24u + f->length) { return 0; }
    uint8_t record[88] = {0};
    put(record + 2, f->flags, 2); put(record + 4, f->id, 4);
    put(record + 8, f->timestamp_us, 8); put(record + 16, f->sequence, 4);
    record[20] = f->dlc; record[21] = f->length;
    memcpy(record + 24, f->data, f->length);
    size_t n = frame(record, 24u + f->length, tx);
    if (n) { memcpy(wire, record, n); }
    return n;
}

size_t xcan_v2_frame_decode(const uint8_t *wire, size_t n, bool tx,
                            struct xcan_v2_can_frame *f)
{
    size_t size = frame(wire, n, tx);
    if (!size) { return 0; }
    f->flags = u16(wire + 2); f->id = u32(wire + 4); f->sequence = u32(wire + 16);
    f->timestamp_us = (uint64_t)u32(wire + 8) | (uint64_t)u32(wire + 12) << 32;
    f->dlc = wire[20]; f->length = wire[21];
    memset(f->data, 0, sizeof(f->data));
    memcpy(f->data, wire + 24, f->length);
    return size;
}

bool xcan_v2_valid(const struct xcan_v2_message *m)
{
    if (!envelope(m)) { return false; }
    const uint8_t *p = m->payload;
    size_t n = m->length;
    if (m->status) { return true; }
    if (m->kind == XCAN_V2_REQUEST) {
        switch (m->opcode) {
        case XCAN_V2_HELLO: case XCAN_V2_STATUS: case XCAN_V2_START: case XCAN_V2_STOP:
        case XCAN_V2_FW_STATUS: case XCAN_V2_FW_FINISH:
        case XCAN_V2_FW_ABORT: case XCAN_V2_FW_REBOOT:
            return n == 0;
        case XCAN_V2_CONFIGURE:
            return n == 12 && u32(p) != 0 && p[8] <= 1 && p[9] <= 1 && zero(p + 10, 2) &&
                   (p[9] ? u32(p + 4) != 0 : u32(p + 4) == 0);
        case XCAN_V2_SEND: return n != 0 && frame(p, n, true) == n;
        case XCAN_V2_FW_BEGIN:
            return n == 36 && u32(p) >= 520 && u32(p) <= 0xc000;
        case XCAN_V2_FW_WRITE:
            return n >= 9 && n <= 520 && u16(p + 4) == n - 8 &&
                   u16(p + 4) <= 512 && zero(p + 6, 2) &&
                   (u32(p) & 7u) == 0;
        default: return true; /* Bounded unknown request: dispatcher replies UNSUPPORTED. */
        }
    }
    if (m->kind == XCAN_V2_RESPONSE) {
        switch (m->opcode) {
        case XCAN_V2_HELLO:
            return n == 16 && !(u32(p) & ~31u) && u16(p + 4) == XCAN_V2_MAX_MESSAGE &&
                   p[6] == 1 && p[7] <= 2 && u32(p + 8) == 1000000 && zero(p + 12, 4);
        case XCAN_V2_STATUS: return state(p, n);
        case XCAN_V2_START: return n == 4 && u32(p) != 0;
        case XCAN_V2_CONFIGURE: case XCAN_V2_STOP: case XCAN_V2_SEND: return n == 0;
        case XCAN_V2_FW_STATUS:
            return n == 16 && p[0] <= 5 && p[1] <= 6 && zero(p + 2, 2);
        case XCAN_V2_FW_BEGIN: case XCAN_V2_FW_WRITE:
        case XCAN_V2_FW_FINISH: case XCAN_V2_FW_ABORT:
        case XCAN_V2_FW_REBOOT: return n == 0;
        default: return true;
        }
    }
    switch (m->opcode) {
    case XCAN_V2_FRAMES: {
        if (n < 8 || !u32(p) || !u16(p + 4) || !zero(p + 6, 2)) { return false; }
        size_t pos = 8;
        for (unsigned int i = 0; i < u16(p + 4); ++i) {
            size_t size = frame(p + pos, n - pos, false);
            if (!size) { return false; }
            pos += size;
        }
        return pos == n;
    }
    case XCAN_V2_STATE: return state(p, n);
    case XCAN_V2_TX_RESULT:
        return n == 16 && u32(p) != 0 && u16(p + 4) <= 3 && zero(p + 6, 2);
    default: return true; /* Unknown event remains framed; host may skip and account sequence. */
    }
}

static bool header(const uint8_t *p, struct xcan_v2_message *m)
{
    if (memcmp(p, "XCAN", 4) || p[4] != 2) { return false; }
    m->kind = p[5]; m->opcode = u16(p + 6); m->length = u16(p + 8);
    m->status = u16(p + 10); m->id = u32(p + 12); m->session = u32(p + 16);
    m->sequence = u32(p + 20);
    return envelope(m);
}

size_t xcan_v2_encode(const struct xcan_v2_message *m, uint8_t *wire, size_t capacity)
{
    if (!xcan_v2_valid(m) || capacity < XCAN_V2_HEADER + m->length) { return 0; }
    memcpy(wire, "XCAN", 4); wire[4] = 2; wire[5] = m->kind;
    put(wire + 6, m->opcode, 2); put(wire + 8, m->length, 2); put(wire + 10, m->status, 2);
    put(wire + 12, m->id, 4); put(wire + 16, m->session, 4); put(wire + 20, m->sequence, 4);
    memcpy(wire + XCAN_V2_HEADER, m->payload, m->length);
    return XCAN_V2_HEADER + m->length;
}

bool xcan_v2_decode(const uint8_t *wire, size_t n, struct xcan_v2_message *m)
{
    if (n < XCAN_V2_HEADER || !header(wire, m) || n != XCAN_V2_HEADER + m->length) { return false; }
    memcpy(m->payload, wire + XCAN_V2_HEADER, m->length);
    return xcan_v2_valid(m);
}

int xcan_v2_feed(struct xcan_v2_decoder *d, const uint8_t *data, size_t n,
                 size_t *consumed, struct xcan_v2_message *m)
{
    *consumed = 0;
    if (d->failed) { return -1; }
    while (*consumed < n) {
        size_t target = XCAN_V2_HEADER;
        if (d->used >= XCAN_V2_HEADER) { target += u16(d->bytes + 8); }
        size_t take = target - d->used;
        if (take > n - *consumed) { take = n - *consumed; }
        memcpy(d->bytes + d->used, data + *consumed, take);
        d->used += take; *consumed += take;
        if (d->used < XCAN_V2_HEADER) { continue; }
        if (!header(d->bytes, m)) { d->failed = true; return -1; }
        if (d->used == XCAN_V2_HEADER + m->length) {
            if (!xcan_v2_decode(d->bytes, d->used, m)) { d->failed = true; return -1; }
            d->used = 0;
            return 1;
        }
    }
    return 0;
}

bool xcan_v2_finish(struct xcan_v2_decoder *d)
{
    if (d->used) { d->failed = true; }
    return !d->failed;
}
