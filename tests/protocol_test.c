#include "protocol.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    uint8_t req[64] = {0}, out[64];
    memcpy(req, "XCAN", 4); req[4] = 1; req[5] = 2; req[8] = 42;
    for (unsigned int n = 0; n <= 52; ++n) {
        req[7] = n;
        for (unsigned int i = 0; i < n; ++i) { req[12+i] = (uint8_t)(i*37); }
        xcan_reply(req, out, 144000000, 0x20036468, 123, 1);
        assert(out[6] == 0 && out[7] == n && out[8] == 42);
        assert(memcmp(out+12, req+12, n) == 0);
        for (unsigned int i = 12+n; i < 64; ++i) { assert(out[i] == 0); }
    }
    req[7] = 53; xcan_reply(req, out, 0, 0, 0, 0); assert(out[6] == 1);
    req[7] = 0; req[4] = 2; xcan_reply(req, out, 0, 0, 0, 0); assert(out[6] == 1);
    req[4] = 1; req[5] = 99; xcan_reply(req, out, 0, 0, 0, 0); assert(out[6] == 2);
    req[5] = 1; xcan_reply(req, out, 144000000, 0x20036468, 123, -1);
    assert(out[6] == 0 && out[7] == 14 && out[24] == 255 && out[25] == 1);
    assert(out[16] == 0x68 && out[17] == 0x64 && out[18] == 3 && out[19] == 0x20);
    return 0;
}
