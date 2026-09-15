#ifndef XCAN_CAN_APP_H
#define XCAN_CAN_APP_H
#include "can_port.h"
enum xcan_output { XCAN_OUTPUT_NONE, XCAN_OUTPUT_RESPONSE, XCAN_OUTPUT_STATE,
                   XCAN_OUTPUT_FRAMES, XCAN_OUTPUT_TX };
struct xcan_app {
    uint32_t generation, session, last_id, stream, event_sequence;
    bool dead, configured, response_ready, prefer_event, state_dirty;
    uint8_t last_state, last_term;
    struct xcan_v2_message response;
};
void xcan_app_reset(struct xcan_app *app, uint32_t generation);
void xcan_app_abort(struct xcan_app *app);
bool xcan_app_request(struct xcan_app *app, const struct xcan_v2_message *request);
void xcan_app_poll(struct xcan_app *app, uint64_t now_ms);
enum xcan_output xcan_app_next(struct xcan_app *app, struct xcan_v2_message *message);
void xcan_app_sent(struct xcan_app *app, enum xcan_output output);
#endif
