#include "boot_memory_map.h"
#include <flash_map_backend/flash_map_backend.h>
#include <stddef.h>
#include <string.h>
#include <stm32g4xx_hal.h>

static const struct flash_area areas[] = {
    {FLASH_AREA_ID_PRIMARY, FLASH_DEVICE_INTERNAL_FLASH, 0, XCAN_PRIMARY_BASE,
     XCAN_PRIMARY_SIZE},
    {FLASH_AREA_ID_SECONDARY, FLASH_DEVICE_INTERNAL_FLASH, 0,
     XCAN_SECONDARY_BASE, XCAN_SECONDARY_SIZE},
};

static const struct flash_area *find_area(uint8_t id) {
  for (unsigned i = 0; i < sizeof(areas) / sizeof(areas[0]); ++i)
    if (areas[i].fa_id == id)
      return &areas[i];
  return NULL;
}

static int valid(const struct flash_area *area, uint32_t off, uint32_t len) {
  return area != NULL && find_area(area->fa_id) == area &&
         off <= area->fa_size && len <= area->fa_size - off;
}

int flash_device_base(uint8_t id, uintptr_t *base) {
  if (id != FLASH_DEVICE_INTERNAL_FLASH || base == NULL)
    return -1;
  *base = 0;
  return 0;
}

int flash_area_open(uint8_t id, const struct flash_area **area) {
  if (area == NULL)
    return -1;
  *area = find_area(id);
  return *area == NULL ? -1 : 0;
}

void flash_area_close(const struct flash_area *area) { (void)area; }

int flash_area_read(const struct flash_area *area, uint32_t off, void *dst,
                    uint32_t len) {
  if (!valid(area, off, len) || (dst == NULL && len != 0U))
    return -1;
  if (len != 0U)
    memcpy(dst, (const void *)(uintptr_t)(area->fa_off + off), len);
  return 0;
}

int flash_area_write(const struct flash_area *area, uint32_t off, const void *src,
                     uint32_t len) {
  if (!valid(area, off, len) || (src == NULL && len != 0U) ||
      (off % XCAN_FLASH_WRITE_SIZE) != 0U ||
      (len % XCAN_FLASH_WRITE_SIZE) != 0U)
    return -1;
  if (len == 0U)
    return 0;

  uint32_t address = area->fa_off + off;
  const uint8_t *bytes = src;
  if (HAL_FLASH_Unlock() != HAL_OK)
    return -1;
  while (len != 0U) {
    uint64_t value;
    memcpy(&value, bytes, sizeof(value));
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, value) !=
        HAL_OK) {
      HAL_FLASH_Lock();
      return -1;
    }
    address += sizeof(value);
    bytes += sizeof(value);
    len -= sizeof(value);
  }
  return HAL_FLASH_Lock() == HAL_OK ? 0 : -1;
}

int flash_area_erase(const struct flash_area *area, uint32_t off, uint32_t len) {
  if (!valid(area, off, len) || len == 0U ||
      (off % XCAN_FLASH_PAGE_SIZE) != 0U ||
      (len % XCAN_FLASH_PAGE_SIZE) != 0U)
    return -1;

  FLASH_EraseInitTypeDef erase = {
      .TypeErase = FLASH_TYPEERASE_PAGES,
      .Banks = FLASH_BANK_1,
      .Page = (area->fa_off + off - XCAN_FLASH_BASE) / XCAN_FLASH_PAGE_SIZE,
      .NbPages = len / XCAN_FLASH_PAGE_SIZE,
  };
  uint32_t error;
  if (HAL_FLASH_Unlock() != HAL_OK)
    return -1;
  if (HAL_FLASHEx_Erase(&erase, &error) != HAL_OK) {
    HAL_FLASH_Lock();
    return -1;
  }
  return HAL_FLASH_Lock() == HAL_OK ? 0 : -1;
}

uint32_t flash_area_align(const struct flash_area *area) {
  return area != NULL && find_area(area->fa_id) == area ? XCAN_FLASH_WRITE_SIZE
                                                        : 0U;
}

uint8_t flash_area_erased_val(const struct flash_area *area) {
  (void)area;
  return 0xFFU;
}

int flash_area_get_sectors(int id, uint32_t *count,
                           struct flash_sector *sectors) {
  const struct flash_area *area =
      id >= 0 && id <= UINT8_MAX ? find_area((uint8_t)id) : NULL;
  if (area == NULL || count == NULL || sectors == NULL)
    return -1;
  uint32_t needed = area->fa_size / XCAN_FLASH_PAGE_SIZE;
  uint32_t capacity = *count;
  *count = needed;
  if (capacity < needed)
    return -1;
  for (uint32_t i = 0; i < needed; ++i) {
    sectors[i].fs_off = i * XCAN_FLASH_PAGE_SIZE;
    sectors[i].fs_size = XCAN_FLASH_PAGE_SIZE;
  }
  return 0;
}

int flash_area_get_sector(const struct flash_area *area, uint32_t off,
                          struct flash_sector *sector) {
  if (area == NULL || find_area(area->fa_id) != area || sector == NULL ||
      off >= area->fa_size)
    return -1;
  sector->fs_off = off - off % XCAN_FLASH_PAGE_SIZE;
  sector->fs_size = XCAN_FLASH_PAGE_SIZE;
  return 0;
}

int flash_area_id_from_multi_image_slot(int image, int slot) {
  if (image != 0)
    return -1;
  return slot == 0 ? FLASH_AREA_ID_PRIMARY
                   : slot == 1 ? FLASH_AREA_ID_SECONDARY : -1;
}

int flash_area_id_from_image_slot(int slot) {
  return flash_area_id_from_multi_image_slot(0, slot);
}

int flash_area_id_to_multi_image_slot(int image, int area) {
  if (image != 0)
    return -1;
  return area == FLASH_AREA_ID_PRIMARY
             ? 0
             : area == FLASH_AREA_ID_SECONDARY ? 1 : -1;
}
