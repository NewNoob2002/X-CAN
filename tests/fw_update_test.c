#include "fw_update.h"
#include "boot_memory_map.h"
#include <flash_map_backend/flash_map_backend.h>
#include <tinycrypt/sha256.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t secondary[XCAN_SECONDARY_SIZE], product[XCAN_PRODUCT_DATA_SIZE];
static const struct flash_area area = {FLASH_AREA_ID_SECONDARY, 0, 0, 0, sizeof(secondary)};
static int pending, erase_fail;

int flash_area_open(uint8_t id, const struct flash_area **result)
{ if(id!=FLASH_AREA_ID_SECONDARY)return -1; *result=&area; return 0; }
void flash_area_close(const struct flash_area *unused) { (void)unused; }
int flash_area_read(const struct flash_area *unused, uint32_t off, void *dst, uint32_t len)
{ if(off>sizeof(secondary)||len>sizeof(secondary)-off)return -1; memcpy(dst,secondary+off,len); return 0; }
int flash_area_write(const struct flash_area *unused, uint32_t off, const void *src, uint32_t len)
{
    if((off|len)&7 || off>sizeof(secondary)||len>sizeof(secondary)-off)return -1;
    const uint8_t *bytes=src;
    for(uint32_t i=0;i<len;i++) { if((secondary[off+i]&bytes[i])!=bytes[i])return -1; }
    for(uint32_t i=0;i<len;i++) secondary[off+i]&=bytes[i];
    return 0;
}
int flash_area_erase(const struct flash_area *unused, uint32_t off, uint32_t len)
{ if(erase_fail || (off|len)&(XCAN_FLASH_PAGE_SIZE-1) || off+len>sizeof(secondary))return -1; memset(secondary+off,0xff,len); return 0; }
int xcan_boot_request_test(void) { pending++; return 0; }

static void hash(const uint8_t *data, size_t length, uint8_t digest[32])
{
    struct tc_sha256_state_struct state;
    assert(tc_sha256_init(&state));
    assert(tc_sha256_update(&state,data,length));
    assert(tc_sha256_final(digest,&state));
}
static void erase_all(uint32_t size, const uint8_t digest[32])
{
    assert(xcan_fw_begin(size,digest)==0);
    struct xcan_fw_status status;
    for(unsigned i=0;i<XCAN_SECONDARY_SIZE/XCAN_FLASH_PAGE_SIZE;i++) xcan_fw_step();
    xcan_fw_get_status(&status);
    assert(status.state==XCAN_FW_READY && status.erase_offset==XCAN_SECONDARY_SIZE);
}
static void write_all(const uint8_t *image, size_t length)
{
    for(size_t offset=0;offset<length;offset+=XCAN_FW_CHUNK_MAX) {
        size_t n=length-offset; if(n>XCAN_FW_CHUNK_MAX)n=XCAN_FW_CHUNK_MAX;
        assert(xcan_fw_write(offset,image+offset,n)==0);
    }
}

int main(void)
{
    uint8_t image[1301], digest[32];
    for(size_t i=0;i<sizeof(image);i++) image[i]=(uint8_t)(i*37u);
    image[0]=0x3d; image[1]=0xb8; image[2]=0xf3; image[3]=0x96;
    hash(image,sizeof(image),digest);
    memset(secondary,0, sizeof(secondary));
    memset(product,0x5a,sizeof(product));
    xcan_fw_reset();
    assert(xcan_fw_begin(519,digest)==-XCAN_FW_ERROR_ARGUMENT);
    assert(xcan_fw_begin(sizeof(image),digest)==0);
    assert(secondary[0]==0 && secondary[XCAN_SECONDARY_SIZE-1]==0xff);
    assert(xcan_fw_write(0,image,512)==-XCAN_FW_ERROR_STATE);
    for(unsigned i=0;i<XCAN_SECONDARY_SIZE/XCAN_FLASH_PAGE_SIZE;i++) xcan_fw_step();
    assert(!memcmp(product,(uint8_t[XCAN_PRODUCT_DATA_SIZE]){[0 ... XCAN_PRODUCT_DATA_SIZE-1]=0x5a},sizeof(product)));
    assert(xcan_fw_write(512,image+512,512)==-XCAN_FW_ERROR_STATE);
    assert(xcan_fw_write(0,image,512)==0);
    assert(xcan_fw_write(0,image,512)==0);
    image[20]^=1; assert(xcan_fw_write(0,image,512)==-XCAN_FW_ERROR_STATE); image[20]^=1;
    assert(xcan_fw_write(512,image+512,512)==0);
    assert(xcan_fw_write(1024,image+1024,sizeof(image)-1024)==0);
    for(size_t i=XCAN_FLASH_PAGE_SIZE+sizeof(image);i<XCAN_FLASH_PAGE_SIZE+((sizeof(image)+7)&~7u);i++) assert(secondary[i]==0xff);
    assert(xcan_fw_finish()==0 && pending==1 && xcan_fw_complete());
    assert(xcan_fw_abort()==-XCAN_FW_ERROR_STATE && xcan_fw_complete());

    uint8_t wrong[32]={0};
    erase_all(sizeof(image),wrong); write_all(image,sizeof(image));
    assert(xcan_fw_finish()==-XCAN_FW_ERROR_HASH);
    struct xcan_fw_status status; xcan_fw_get_status(&status);
    assert(status.state==XCAN_FW_ERROR && status.error==XCAN_FW_ERROR_HASH);

    uint8_t bad_magic[sizeof(image)]; memcpy(bad_magic,image,sizeof(image)); bad_magic[0]=0;
    hash(bad_magic,sizeof(bad_magic),digest);
    erase_all(sizeof(bad_magic),digest); write_all(bad_magic,sizeof(bad_magic));
    assert(xcan_fw_finish()==-XCAN_FW_ERROR_MAGIC);
    assert(xcan_fw_abort()==0); xcan_fw_get_status(&status); assert(status.state==XCAN_FW_IDLE);

    erase_fail=1; assert(xcan_fw_begin(sizeof(image),digest)==-XCAN_FW_ERROR_IO);
    xcan_fw_get_status(&status); assert(status.state==XCAN_FW_ERROR && status.error==XCAN_FW_ERROR_IO);
    puts("PASS firmware update: bounded erase/write, 512-byte chunks, idempotency, hash/magic/pending, product isolation");
}
