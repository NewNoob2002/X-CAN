/* SPDX-License-Identifier: Apache-2.0 */
#ifndef XCAN_FW_UPDATE_H
#define XCAN_FW_UPDATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XCAN_FW_CHUNK_MAX 512u
#define XCAN_FW_HASH_SIZE 32u

enum xcan_fw_state {
    XCAN_FW_IDLE,
    XCAN_FW_ERASING,
    XCAN_FW_READY,
    XCAN_FW_RECEIVING,
    XCAN_FW_COMPLETE,
    XCAN_FW_ERROR,
};

enum xcan_fw_error {
    XCAN_FW_ERROR_NONE,
    XCAN_FW_ERROR_ARGUMENT,
    XCAN_FW_ERROR_STATE,
    XCAN_FW_ERROR_IO,
    XCAN_FW_ERROR_MAGIC,
    XCAN_FW_ERROR_HASH,
    XCAN_FW_ERROR_PENDING,
};

struct xcan_fw_status {
    uint8_t state, error;
    uint32_t image_size, next_offset, erase_offset;
};

void xcan_fw_reset(void);
int xcan_fw_begin(uint32_t image_size, const uint8_t hash[XCAN_FW_HASH_SIZE]);
void xcan_fw_step(void);
int xcan_fw_write(uint32_t offset, const uint8_t *data, size_t length);
int xcan_fw_finish(void);
int xcan_fw_abort(void);
void xcan_fw_get_status(struct xcan_fw_status *status);
bool xcan_fw_complete(void);

#endif
