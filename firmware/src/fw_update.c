/* SPDX-License-Identifier: Apache-2.0 */
#include "fw_update.h"
#include "app.h"
#include "boot_memory_map.h"
#include <bootutil/image.h>
#include <flash_map_backend/flash_map_backend.h>
#include <string.h>
#include <tinycrypt/sha256.h>

static struct {
    struct xcan_fw_status status;
    uint8_t expected_hash[XCAN_FW_HASH_SIZE];
    uint8_t buffer[XCAN_FW_CHUNK_MAX];
} update;

static int fail(enum xcan_fw_error error)
{
    update.status.state = XCAN_FW_ERROR;
    update.status.error = error;
    return -(int)error;
}

void xcan_fw_reset(void) { memset(&update, 0, sizeof(update)); }

int xcan_fw_begin(uint32_t image_size, const uint8_t hash[XCAN_FW_HASH_SIZE])
{
    if (hash == NULL || image_size < XCAN_IMAGE_HEADER_SIZE + 8u ||
        image_size > XCAN_SIGNED_IMAGE_MAX)
        return -(int)XCAN_FW_ERROR_ARGUMENT;
    if (update.status.state == XCAN_FW_ERASING ||
        update.status.state == XCAN_FW_READY ||
        update.status.state == XCAN_FW_RECEIVING) {
        return update.status.image_size == image_size &&
                       memcmp(update.expected_hash, hash, XCAN_FW_HASH_SIZE) == 0
                   ? 0
                   : -(int)XCAN_FW_ERROR_STATE;
    }
    memset(&update, 0, sizeof(update));
    update.status.state = XCAN_FW_ERASING;
    update.status.image_size = image_size;
    memcpy(update.expected_hash, hash, XCAN_FW_HASH_SIZE);
    const struct flash_area *area;
    if (flash_area_open(FLASH_AREA_ID_SECONDARY, &area) != 0)
        return fail(XCAN_FW_ERROR_IO);
    int result = flash_area_erase(area, XCAN_SECONDARY_SIZE - XCAN_FLASH_PAGE_SIZE,
                                  XCAN_FLASH_PAGE_SIZE);
    flash_area_close(area);
    if (result != 0)
        return fail(XCAN_FW_ERROR_IO);
    update.status.erase_offset = XCAN_FLASH_PAGE_SIZE;
    return 0;
}

void xcan_fw_step(void)
{
    if (update.status.state != XCAN_FW_ERASING)
        return;
    const struct flash_area *area;
    if (flash_area_open(FLASH_AREA_ID_SECONDARY, &area) != 0) {
        fail(XCAN_FW_ERROR_IO);
        return;
    }
    int result = flash_area_erase(area, update.status.erase_offset - XCAN_FLASH_PAGE_SIZE,
                                  XCAN_FLASH_PAGE_SIZE);
    flash_area_close(area);
    if (result != 0) {
        fail(XCAN_FW_ERROR_IO);
        return;
    }
    update.status.erase_offset += XCAN_FLASH_PAGE_SIZE;
    if (update.status.erase_offset == XCAN_SECONDARY_SIZE)
        update.status.state = XCAN_FW_READY;
}

int xcan_fw_write(uint32_t offset, const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0 || length > XCAN_FW_CHUNK_MAX ||
        offset % XCAN_FLASH_WRITE_SIZE != 0 ||
        offset > update.status.image_size ||
        length > update.status.image_size - offset ||
        (length % XCAN_FLASH_WRITE_SIZE != 0 &&
         offset + length != update.status.image_size))
        return -(int)XCAN_FW_ERROR_ARGUMENT;
    if (update.status.state == XCAN_FW_ERASING)
        return -(int)XCAN_FW_ERROR_STATE;
    if (update.status.state != XCAN_FW_READY &&
        update.status.state != XCAN_FW_RECEIVING)
        return -(int)XCAN_FW_ERROR_STATE;

    const struct flash_area *area;
    if (flash_area_open(FLASH_AREA_ID_SECONDARY, &area) != 0)
        return fail(XCAN_FW_ERROR_IO);
    uint32_t area_offset = XCAN_FLASH_PAGE_SIZE + offset;
    if (offset < update.status.next_offset) {
        int result = offset + length <= update.status.next_offset &&
                             flash_area_read(area, area_offset, update.buffer, length) == 0 &&
                             memcmp(update.buffer, data, length) == 0
                         ? 0
                         : -(int)XCAN_FW_ERROR_STATE;
        flash_area_close(area);
        return result;
    }
    if (offset != update.status.next_offset) {
        flash_area_close(area);
        return -(int)XCAN_FW_ERROR_STATE;
    }

    size_t programmed = (length + XCAN_FLASH_WRITE_SIZE - 1u) &
                        ~(size_t)(XCAN_FLASH_WRITE_SIZE - 1u);
    memset(update.buffer, 0xff, programmed);
    memcpy(update.buffer, data, length);
    int result = flash_area_write(area, area_offset, update.buffer, programmed);
    flash_area_close(area);
    if (result != 0)
        return fail(XCAN_FW_ERROR_IO);
    update.status.next_offset += length;
    update.status.state = XCAN_FW_RECEIVING;
    return 0;
}

int xcan_fw_finish(void)
{
    if (update.status.state != XCAN_FW_RECEIVING ||
        update.status.next_offset != update.status.image_size)
        return -(int)XCAN_FW_ERROR_STATE;
    const struct flash_area *area;
    uint32_t magic;
    if (flash_area_open(FLASH_AREA_ID_SECONDARY, &area) != 0)
        return fail(XCAN_FW_ERROR_IO);
    if (flash_area_read(area, XCAN_FLASH_PAGE_SIZE, &magic, sizeof(magic)) != 0) {
        flash_area_close(area);
        return fail(XCAN_FW_ERROR_IO);
    }
    if (magic != IMAGE_MAGIC) {
        flash_area_close(area);
        return fail(XCAN_FW_ERROR_MAGIC);
    }

    struct tc_sha256_state_struct sha;
    uint8_t digest[XCAN_FW_HASH_SIZE];
    if (!tc_sha256_init(&sha)) {
        flash_area_close(area);
        return fail(XCAN_FW_ERROR_IO);
    }
    uint32_t offset = 0;
    while (offset < update.status.image_size) {
        size_t length = update.status.image_size - offset;
        if (length > sizeof(update.buffer))
            length = sizeof(update.buffer);
        if (flash_area_read(area, XCAN_FLASH_PAGE_SIZE + offset, update.buffer, length) != 0 ||
            !tc_sha256_update(&sha, update.buffer, length)) {
            flash_area_close(area);
            return fail(XCAN_FW_ERROR_IO);
        }
        offset += length;
    }
    flash_area_close(area);
    if (!tc_sha256_final(digest, &sha))
        return fail(XCAN_FW_ERROR_IO);
    if (memcmp(digest, update.expected_hash, sizeof(digest)) != 0)
        return fail(XCAN_FW_ERROR_HASH);
    if (xcan_boot_request_test() != 0)
        return fail(XCAN_FW_ERROR_PENDING);
    update.status.state = XCAN_FW_COMPLETE;
    update.status.error = XCAN_FW_ERROR_NONE;
    return 0;
}

int xcan_fw_abort(void)
{
    if (update.status.state == XCAN_FW_COMPLETE)
        return -(int)XCAN_FW_ERROR_STATE;
    xcan_fw_reset();
    return 0;
}

void xcan_fw_get_status(struct xcan_fw_status *status)
{
    if (status != NULL)
        *status = update.status;
}

bool xcan_fw_complete(void) { return update.status.state == XCAN_FW_COMPLETE; }
