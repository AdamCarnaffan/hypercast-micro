/*
 * This file is designed to handle general work that spans across protocols, then hand the packet off
 * to the appropriate protocol handler. It will then do all of the remaining work to understand what was received,
 * and will then set things up as appropriate
 */
#include "esp_log.h"

#include "hc_buffer.h"
#include "hc_protocols.h"

// Protocol Includes
#include "spt.h"

void hc_protocol_parse(hc_packet_t *packet, long protocolId) {
    // First get the OverlayID hash, and the type, which are common to all protocols
    long messageLength = packet_to_int(packet_snip_to_bytes(packet, 16, 8));
    long protocolMessageType = packet_to_int(packet_snip_to_bytes(packet, 8, 24));
    long overlayId = packet_to_int(packet_snip_to_bytes(packet, 32, 32));
    switch (protocolId) {
        case HC_PROTOCOL_SPT:
            spt_parse(packet, protocolMessageType, overlayId, messageLength);
            break;
        default:
            ESP_LOGE(TAG, "MESSAGE FROM UNSUPPORTED PROTOCOL RECEIVED");
            break;
    }
}