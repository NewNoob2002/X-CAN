#include "protocol_v2.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void stream(const uint8_t *wire, size_t length, size_t chunk, bool valid)
{
    struct xcan_v2_decoder d = {0};
    struct xcan_v2_message m;
    size_t offset = 0, count = 0;
    int result = 0;
    while (offset < length && result >= 0) {
        size_t n = length - offset, consumed;
        if (n > chunk) { n = chunk; }
        result = xcan_v2_feed(&d, wire + offset, n, &consumed, &m);
        assert(consumed <= n);
        assert(consumed || result < 0);
        offset += consumed;
        if (result == 1) { count++; }
    }
    assert((xcan_v2_finish(&d) && count == 1) == valid);
    if (d.failed) {
        size_t consumed = 99;
        assert(xcan_v2_feed(&d, wire, length, &consumed, &m) == -1 && consumed == 0);
    }
}

static void record_roundtrip(const struct xcan_v2_message *m)
{
    bool tx = m->kind == XCAN_V2_REQUEST && m->opcode == XCAN_V2_SEND;
    if (!tx && !(m->kind == XCAN_V2_EVENT && m->opcode == XCAN_V2_FRAMES)) { return; }
    size_t pos = tx ? 0 : 8;
    while (pos < m->length) {
        struct xcan_v2_can_frame f;
        uint8_t wire[88];
        size_t n = xcan_v2_frame_decode(m->payload + pos, m->length - pos, tx, &f);
        assert(n && xcan_v2_frame_encode(&f, tx, wire, sizeof(wire)) == n);
        assert(memcmp(wire, m->payload + pos, n) == 0);
        assert(xcan_v2_frame_encode(&f, tx, wire, n - 1) == 0);
        pos += n;
    }
    assert(pos == m->length);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "r");
    assert(file);
    char line[4096], name[80], hex[3000];
    unsigned int vectors = 0;
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#') { continue; }
        int valid;
        assert(sscanf(line, "%d %79s %2999s", &valid, name, hex) == 3);
        size_t n = strlen(hex) / 2;
        uint8_t wire[1500], encoded[XCAN_V2_MAX_MESSAGE];
        assert(n <= sizeof(wire) && strlen(hex) % 2 == 0);
        for (size_t i = 0; i < n; ++i) {
            unsigned int value;
            assert(sscanf(hex + i * 2, "%2x", &value) == 1); wire[i] = (uint8_t)value;
        }
        struct xcan_v2_message m;
        bool result = xcan_v2_decode(wire, n, &m);
        if (result != (bool)valid) { fprintf(stderr, "vector failed: %s\n", name); abort(); }
        for (size_t chunk = 1; chunk <= n; ++chunk) { stream(wire, n, chunk, valid); }
        if (valid) {
            assert(xcan_v2_encode(&m, encoded, sizeof(encoded)) == n);
            assert(memcmp(encoded, wire, n) == 0);
            assert(xcan_v2_encode(&m, encoded, n - 1) == 0);
            record_roundtrip(&m);
            uint8_t joined[2 * XCAN_V2_MAX_MESSAGE];
            memcpy(joined, wire, n); memcpy(joined + n, wire, n);
            struct xcan_v2_decoder d = {0};
            size_t consumed;
            assert(xcan_v2_feed(&d, joined, 2 * n, &consumed, &m) == 1 && consumed == n);
            assert(xcan_v2_feed(&d, joined + n, n, &consumed, &m) == 1 && consumed == n);
            assert(d.used == 0 && !d.failed);
        }
        vectors++;
    }
    assert(!ferror(file)); fclose(file);
    struct xcan_v2_can_frame invalid = {.id = 0x800, .dlc = 0};
    uint8_t untouched[88]; memset(untouched, 0xa5, sizeof(untouched));
    assert(xcan_v2_frame_encode(&invalid, false, untouched, sizeof(untouched)) == 0);
    for (size_t i = 0; i < sizeof(untouched); ++i) { assert(untouched[i] == 0xa5); }
    printf("PASS C v2: %u shared vectors, all chunk sizes, coalescing, frame codecs, bounded errors\n", vectors);
    return 0;
}
