/* X-CAN single-owner CAN backend. ISR callbacks only enqueue/capture results. */
#ifndef XCAN_CAN_PORT_H
#define XCAN_CAN_PORT_H
#include "protocol_v2.h"
struct xcan_can_status {
    uint8_t state, mode, term;
    uint32_t nominal, data;
    uint64_t received, dropped, overruns;
};
struct xcan_tx_result { uint32_t id; uint16_t result; uint64_t timestamp; };
int xcan_can_init(void);
void xcan_can_gate_off(void);
void xcan_can_reset(uint32_t generation);
int xcan_can_configure(uint32_t nominal, uint32_t data, uint8_t mode, bool fd);
int xcan_can_start(void);
int xcan_can_stop(void);
int xcan_can_send(uint32_t id, const struct xcan_v2_can_frame *frame);
void xcan_can_poll(uint64_t now_ms);
void xcan_can_status(struct xcan_can_status *status);
bool xcan_can_pop(struct xcan_v2_can_frame *frame);
bool xcan_can_tx_result(struct xcan_tx_result *result);
void xcan_can_tx_delivered(void);
bool xcan_usb_active(uint32_t generation);
#endif
