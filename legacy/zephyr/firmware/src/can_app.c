#include "can_app.h"
#include <errno.h>
#include <string.h>

static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put(uint8_t *p, uint64_t v, unsigned int n)
{ for (unsigned int i = 0; i < n; i++) { p[i] = (uint8_t)(v >> (8*i)); } }
static uint16_t error(int err)
{
    if (!err) { return 0; }
    if (err == -ENOTSUP) { return 2; }
    if (err == -EBUSY || err == -EAGAIN) { return 4; }
    if (err == -ENETDOWN || err == -ENETUNREACH || err == -EALREADY) { return 3; }
    if (err == -EINVAL || err == -ERANGE) { return 1; }
    return 6;
}
static void status_payload(struct xcan_app *a, struct xcan_v2_message *m)
{
    struct xcan_can_status s;
    xcan_can_status(&s);
    m->length = 40;
    memset(m->payload, 0, 40);
    m->payload[0] = s.state; m->payload[1] = s.mode; m->payload[2] = s.term;
    put(m->payload+4, a->stream, 4); put(m->payload+8, s.nominal, 4); put(m->payload+12, s.data, 4);
    put(m->payload+16, s.received, 8); put(m->payload+24, s.dropped, 8); put(m->payload+32, s.overruns, 8);
}
void xcan_app_reset(struct xcan_app *a, uint32_t generation)
{
    xcan_can_reset(generation);
    memset(a, 0, sizeof(*a));
    a->generation = generation;
    a->last_state = a->last_term = 255;
}
void xcan_app_abort(struct xcan_app *a)
{
    xcan_can_gate_off();
    (void)xcan_can_stop();
    a->dead = true;
    a->session = 0;
    a->response_ready = false;
}
bool xcan_app_request(struct xcan_app *a, const struct xcan_v2_message *q)
{
    if (a->dead || a->response_ready || q->kind != XCAN_V2_REQUEST || !xcan_v2_valid(q)) { return false; }
    struct xcan_v2_message *r = &a->response;
    memset(r, 0, sizeof(*r));
    r->kind = XCAN_V2_RESPONSE; r->opcode = q->opcode; r->id = q->id; r->session = q->session;
    a->response_ready = true;
    if (q->opcode == XCAN_V2_HELLO) {
        if (a->session) { r->status = 3; return true; }
        a->session = a->generation;
        a->last_id = q->id;
        r->session = a->session; r->length = 16;
        put(r->payload, 15, 4); put(r->payload+4, 512, 2);
        r->payload[6] = 1; /* One channel; timestamp source=software callback. */
        put(r->payload+8, 1000000, 4);
        return true;
    }
    if (!a->session || q->session != a->session) { r->status = 5; return true; }
    if (q->id <= a->last_id) { r->status = 3; return true; }
    a->last_id = q->id;
    int err = 0;
    switch (q->opcode) {
    case XCAN_V2_STATUS: status_payload(a, r); break;
    case XCAN_V2_CONFIGURE:
        err = xcan_can_configure(get32(q->payload), get32(q->payload+4), q->payload[8], q->payload[9]);
        if (!err) { a->configured = true; }
        else if (err != -EBUSY && err != -ENETDOWN) { a->configured = false; }
        break;
    case XCAN_V2_START:
        if (!a->configured) { err = -ENETDOWN; break; }
        if (a->stream == UINT32_MAX) { err = -ERANGE; break; }
        err = xcan_can_start();
        if (!err) { a->stream++; r->length = 4; put(r->payload, a->stream, 4); a->state_dirty = true; }
        break;
    case XCAN_V2_STOP:
        err = xcan_can_stop(); a->state_dirty = true; break;
    case XCAN_V2_SEND: {
        struct xcan_v2_can_frame f;
        if (!xcan_v2_frame_decode(q->payload, q->length, true, &f)) { return false; }
        err = xcan_can_send(q->id, &f); break;
    }
    default: r->status = 2; return true;
    }
    r->status = error(err);
    if (r->status) { r->length = 0; }
    return true;
}
void xcan_app_poll(struct xcan_app *a, uint64_t now_ms)
{
    xcan_can_poll(now_ms);
    struct xcan_can_status s;
    xcan_can_status(&s);
    if (s.state != a->last_state || s.term != a->last_term) {
        a->last_state = s.state; a->last_term = s.term; a->state_dirty = true;
    }
}
enum xcan_output xcan_app_next(struct xcan_app *a, struct xcan_v2_message *m)
{
    if (a->dead) { return XCAN_OUTPUT_NONE; }
    if (a->response_ready && (!a->prefer_event || !a->session || a->response.opcode == XCAN_V2_START ||
                             a->response.opcode == XCAN_V2_HELLO || a->response.opcode == XCAN_V2_SEND)) {
        *m = a->response; return XCAN_OUTPUT_RESPONSE;
    }
    if (a->session) {
        memset(m, 0, sizeof(*m)); m->kind = XCAN_V2_EVENT; m->session = a->session;
        m->sequence = a->event_sequence;
        struct xcan_tx_result tx;
        enum xcan_output output = XCAN_OUTPUT_NONE;
        if (xcan_can_tx_result(&tx)) {
            m->opcode = XCAN_V2_TX_RESULT; m->length = 16;
            put(m->payload, tx.id, 4); put(m->payload+4, tx.result, 2); put(m->payload+8, tx.timestamp, 8);
            output = XCAN_OUTPUT_TX;
        } else if (a->state_dirty) {
            m->opcode = XCAN_V2_STATE; status_payload(a, m); a->state_dirty = false;
            output = XCAN_OUTPUT_STATE;
        } else {
            struct xcan_v2_can_frame f;
            size_t pos = 8;
            unsigned int count = 0;
            /* At most five records, so a popped FD frame always fits. */
            while (count < 5 && xcan_can_pop(&f)) {
                size_t n = xcan_v2_frame_encode(&f, false, m->payload+pos, sizeof(m->payload)-pos);
                if (!n) { xcan_app_abort(a); return XCAN_OUTPUT_NONE; }
                pos += n; count++;
            }
            if (count) {
                m->opcode = XCAN_V2_FRAMES; m->length = pos;
                put(m->payload, a->stream, 4); put(m->payload+4, count, 2);
                output = XCAN_OUTPUT_FRAMES;
            }
        }
        if (output != XCAN_OUTPUT_NONE) { a->event_sequence++; return output; }
    }
    if (a->response_ready) { *m = a->response; return XCAN_OUTPUT_RESPONSE; }
    return XCAN_OUTPUT_NONE;
}
void xcan_app_sent(struct xcan_app *a, enum xcan_output output)
{
    if (output == XCAN_OUTPUT_RESPONSE) { a->response_ready = false; a->prefer_event = true; }
    else { a->prefer_event = false; }
    if (output == XCAN_OUTPUT_TX) { xcan_can_tx_delivered(); }
}
