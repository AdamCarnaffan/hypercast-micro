#ifndef __HC_PROTOCOLS_H__
#define __HC_PROTOCOLS_H__

#define HC_PROTOCOL_OVERLAY_MESSAGE 13
// Then supported protocolIDs
#define HC_PROTOCOL_SPT 3 

#include "hypercast.h"

void hc_protocol_parse(hc_packet_t*, long, hypercast_t*);
void hc_protocol_maintenance(hypercast_t*);
void* resolve_protocol_to_install(int, uint32_t);

#endif

#ifndef TAG
#define TAG "HC_PROTOCOLS"
#endif