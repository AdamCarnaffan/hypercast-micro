#ifndef __HC_ENGINE_H__
#define __HC_ENGINE_H__

#define HC_OVERLAY_PACKET_LENGTH 14
#define HC_PROTOCOL_PACKET_LENGTH 35

void hc_engine_handler(hypercast_t *hypercast);
void hc_forward(hc_packet_t*, hypercast_t*);

#endif

#ifndef TAG
#define TAG "HC_ENGINE"
#endif