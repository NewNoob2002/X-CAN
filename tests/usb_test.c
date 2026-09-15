/* Real CherryUSB core/application; fake controller. No HIL claim. */
#include "usbd_core.h"
#include "app.h"
#include "fw_update.h"
#include "protocol.h"
#include "protocol_v2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t *rx[2];
static uint32_t capacity[2], txlen[2], writes[2];
static uint8_t tx[2][256];
static bool stalled[256], opened[256];
static int fail_read, fail_write, term = -1, locked;
static struct xcan_fw_status fw;
static unsigned fw_write_length, rebooted;
int usb_dc_init(uint8_t b) { return 0; }
int usb_dc_deinit(uint8_t b) { return 0; }
int usbd_set_address(uint8_t b, uint8_t a) { return 0; }
int usbd_set_remote_wakeup(uint8_t b) { return 0; }
uint8_t usbd_get_port_speed(uint8_t b) { return USB_SPEED_FULL; }
int usbd_ep_open(uint8_t b, const struct usb_endpoint_descriptor *ep)
{ opened[ep->bEndpointAddress] = true; return 0; }
int usbd_ep_close(uint8_t b, uint8_t ep) { opened[ep] = false; return 0; }
int usbd_ep_set_stall(uint8_t b, uint8_t ep) { stalled[ep] = true; return 0; }
int usbd_ep_clear_stall(uint8_t b, uint8_t ep) { stalled[ep] = false; return 0; }
int usbd_ep_is_stalled(uint8_t b, uint8_t ep, uint8_t *s) { *s = stalled[ep]; return 0; }
int usbd_ep_start_read(uint8_t b, uint8_t ep, uint8_t *p, uint32_t n)
{
    assert(ep < 2 && opened[ep]);
    if (ep && fail_read) return -1;
    rx[ep] = p; capacity[ep] = n; return 0;
}
int usbd_ep_start_write(uint8_t b, uint8_t ep, const uint8_t *p, uint32_t n)
{
    assert(ep >= 0x80 && ep <= 0x81 && opened[ep] && n <= 256);
    if (ep == 0x81 && fail_write) return -1;
    unsigned i = ep & 1;
    if (n) memcpy(tx[i], p, n);
    txlen[i] = n; writes[i]++; return 0;
}
void usb_wake(void) { }
void usb_lock(void) { assert(!locked); locked = 1; }
void usb_unlock(void) { assert(locked); locked = 0; }
uint32_t board_id(void) { return 0x20036468; }
uint32_t uptime_ms(void) { return 123; }
int termination_get(void) { return term; }
int xcan_boot_request_test(void) { return 0; }
int xcan_boot_confirm(void) { return 0; }
bool xcan_update_ready(void) { return true; }
void xcan_reboot(void) { rebooted++; }
void xcan_fw_reset(void) { memset(&fw, 0, sizeof(fw)); }
int xcan_fw_begin(uint32_t size, const uint8_t hash[32])
{ fw.state=XCAN_FW_ERASING; fw.image_size=size; return hash ? 0 : -1; }
void xcan_fw_step(void)
{ fw.erase_offset += 0x800; if (fw.erase_offset == 0xd000) fw.state=XCAN_FW_READY; }
int xcan_fw_write(uint32_t offset, const uint8_t *data, size_t length)
{ fw_write_length=length; fw.next_offset=offset+length; fw.state=XCAN_FW_RECEIVING; return data ? 0 : -1; }
int xcan_fw_finish(void) { fw.state=XCAN_FW_COMPLETE; return 0; }
int xcan_fw_abort(void) { xcan_fw_reset(); return 0; }
void xcan_fw_get_status(struct xcan_fw_status *status) { *status=fw; }
bool xcan_fw_complete(void) { return fw.state == XCAN_FW_COMPLETE; }
static void setup(uint8_t type, uint8_t req, uint16_t value, uint16_t index, uint16_t len)
{
    uint8_t s[] = {type,req,value,value>>8,index,index>>8,len,len>>8};
    stalled[0x80] = false;
    usbd_event_ep0_setup_complete_handler(0, s);
}
static void configure(void)
{
    setup(0,9,1,0,0); assert(!stalled[0x80]);
    usbd_event_ep_in_complete_handler(0,0x80,0);
    assert(usb_device_is_configured(0));
}
static void reset(void)
{
    memset(opened,0,sizeof(opened)); memset(rx,0,sizeof(rx));
    usbd_event_reset_handler(0);
}
static void send(const uint8_t *p, unsigned n)
{
    assert(rx[1] && n <= capacity[1]);
    memcpy(rx[1],p,n); rx[1] = NULL;
    usbd_event_ep_out_complete_handler(0,1,n);
}
static void check_reply(const uint8_t req[64])
{
    uint8_t expected[64];
    xcan_reply(req,expected,144000000,board_id(),123,term);
    unsigned previous = writes[1]; usb_work();
    assert(writes[1] == previous+1 && txlen[1] == 64);
    assert(!memcmp(expected,tx[1],64));
    assert(!rx[1]);
    usbd_event_ep_in_complete_handler(0,0x81,64);
}
static struct xcan_v2_message exchange_v2(struct xcan_v2_message *message)
{
    uint8_t wire[XCAN_V2_MAX_MESSAGE];
    size_t length=xcan_v2_encode(message,wire,sizeof(wire)); assert(length);
    for(size_t pos=0;pos<length;) {
        size_t n=length-pos>64?64:length-pos; send(wire+pos,n); pos+=n;
    }
    unsigned previous=writes[1]; usb_work();
    assert(writes[1]==previous+1 && txlen[1]<=sizeof(tx[1]));
    struct xcan_v2_message response_message;
    assert(xcan_v2_decode(tx[1],txlen[1],&response_message));
    assert(!rx[1]);
    usbd_event_ep_in_complete_handler(0,0x81,txlen[1]);
    return response_message;
}
int main(void)
{
    const uint32_t uid[] = {0x002d0055,0x58315010,0x20363650};
    assert(usb_init(uid) == 0); reset();
    setup(0x80,6,0x100,0,18); assert(txlen[0] == 18 && tx[0][8] == 0xca && tx[0][9] == 0xc0);
    setup(0x80,6,0x200,0,255); assert(txlen[0] == 32 && tx[0][20] == 1 && tx[0][27] == 0x81);
    setup(0x80,6,0x303,0x409,255); assert(txlen[0] == 50);
    for (unsigned i = 0; i < 24; i++) { assert(tx[0][2+2*i] == "2036365058315010002D0055"[i]); assert(tx[0][3+2*i] == 0); }
    setup(0x80,6,0xf00,0,255); assert(txlen[0] == 33 && tx[0][31] == 2);
    uint8_t msos[162]; setup(0xc0,2,0,7,162); assert(txlen[0] == 162); memcpy(msos,tx[0],162);
    assert(msos[0] == 10 && !memcmp(msos+14,"WINUSB",6));
    unsigned lengths[] = {1,10,64,162,255,65535};
    for (unsigned i=0;i<6;i++) {
        setup(0xc0,2,0,7,lengths[i]); assert(!stalled[0x80]);
        assert(txlen[0] == (lengths[i] < 162 ? lengths[i] : 162));
        assert(!memcmp(tx[0],msos,txlen[0]));
        usbd_event_ep_in_complete_handler(0,0x80,txlen[0]);
        usbd_event_ep_out_complete_handler(0,0,0);
    }
    setup(0xc0,2,1,7,162); assert(stalled[0x80]);
    setup(0xc0,2,0,8,162); assert(stalled[0x80]);
    setup(0xc1,2,0,7,162); assert(stalled[0x80]);
    setup(0xc0,2,0,7,162); assert(!stalled[0x80]);
    setup(0xc0,3,0,0,8); assert(txlen[0]==8 && !memcmp(tx[0],"XCAN\2\0\40\2",8));
    configure();
    uint8_t req[64] = {'X','C','A','N',1,2,0,0,42};
    unsigned count = 0;
    for (unsigned n=0;n<=52;n++) {
        req[7]=n;
        for(unsigned i=0;i<n;i++) req[12+i]=(uint8_t)(i*37);
        for(unsigned chunk=1;chunk<=64;chunk++) {
            for(unsigned pos=0;pos<64;) { unsigned len=64-pos < chunk ? 64-pos : chunk; send(req+pos,len); pos+=len; }
            check_reply(req); count++;
        }
    }
    req[7]=53; send(req,64); check_reply(req);
    req[7]=0; req[5]=99; send(req,64); check_reply(req);
    req[5]=1;
    for(term=-1;term<=1;term++) { send(req,64); check_reply(req); }
    send(req,0); send(req,64); check_reply(req);
    send(req,7); usbd_event_disconnect_handler(0); usb_work();
    reset(); configure(); send(req,64); check_reply(req);
    send(req,7); reset(); configure(); send(req,64); check_reply(req);
    send(req,64); unsigned before=writes[1]; reset(); usb_work(); assert(writes[1]==before);
    configure(); send(req,64); check_reply(req);
    send(req,64); before=writes[1]; setup(0,9,0,0,0); usb_work(); assert(writes[1]==before);
    configure(); send(req,64); check_reply(req);
    send(req,7); send(req,64); uint8_t bad[64]={0}; bad[5]=0xff; check_reply(bad);
    send(req,64); fail_write=1; before=writes[1]; usb_work(); assert(writes[1]==before);
    fail_write=0; reset(); configure(); send(req,64); check_reply(req);
    fail_read=1; reset(); configure(); assert(!rx[1]);
    fail_read=0; reset(); configure(); send(req,64); check_reply(req);
    struct xcan_v2_message message={.kind=XCAN_V2_REQUEST,.opcode=XCAN_V2_HELLO,.id=1};
    struct xcan_v2_message reply=exchange_v2(&message);
    assert(reply.status==0 && reply.length==16 && reply.session!=0);
    uint32_t v2_session=reply.session;
    message=(struct xcan_v2_message){.kind=XCAN_V2_REQUEST,.opcode=XCAN_V2_FW_BEGIN,
        .length=36,.id=2,.session=v2_session};
    message.payload[0]=0x08; message.payload[1]=0x02;
    reply=exchange_v2(&message); assert(reply.status==0);
    message=(struct xcan_v2_message){.kind=XCAN_V2_REQUEST,.opcode=XCAN_V2_FW_WRITE,
        .length=520,.id=3,.session=v2_session};
    message.payload[4]=0x00; message.payload[5]=0x02;
    memset(message.payload+8,0xa5,512);
    fw.state=XCAN_FW_READY;
    reply=exchange_v2(&message); assert(reply.status==0 && fw_write_length==512);
    message=(struct xcan_v2_message){.kind=XCAN_V2_REQUEST,.opcode=XCAN_V2_FW_FINISH,
        .id=4,.session=v2_session};
    reply=exchange_v2(&message); assert(reply.status==0 && xcan_fw_complete());
    message=(struct xcan_v2_message){.kind=XCAN_V2_REQUEST,.opcode=XCAN_V2_FW_REBOOT,
        .id=5,.session=v2_session};
    reply=exchange_v2(&message); assert(reply.status==0 && rebooted==1);
    puts("PASS real CherryUSB core + adapter: descriptors, MSOS strict requests/recovery, 3392 echo chunk cases, INFO states, malformed framing, reset/deconfigure, enqueue failures");
    assert(count==3392);
}
