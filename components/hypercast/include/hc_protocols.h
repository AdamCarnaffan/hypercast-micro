#ifndef __HC_PROTOCOLS_H__
#define __HC_PROTOCOLS_H__

#define HC_PROTOCOL_OVERLAY_MESSAGE 13
// Then supported protocolIDs
#define HC_PROTOCOL_SPT 3 

void hc_protocol_parse(hc_packet_t *, long);

#endif

#ifndef TAG
#define TAG "HC_PROTOCOLS"
#endif