/*
 * This file is designed to handle general work that spans across protocols, then hand the packet off
 * to the appropriate protocol handler. It will then do all of the remaining work to understand what was received,
 * and will then set things up as appropriate
 */
#include "esp_log.h"

#include "hc_buffer.h"
#include "hc_protocols.h"
#include "hc_measure.h"

// Protocol Includes
#include "spt.h"

void hc_protocol_parse(hc_packet_t *packet, long protocolId, hypercast_t *hypercast) {
    // Now check that hypercast protocol matches the message protocol
    if (protocolId != ((hc_protocol_shell_t*)(hypercast->protocol))->id) {
        ESP_LOGE(TAG, "Protocol not active, expected %d, got %ld", (int)((hc_protocol_shell_t*)(hypercast->protocol))->id, protocolId);
        return;
    }
    // Then get the OverlayID hash, and the type, which are common to all protocols
    long messageLength = packet_to_int(packet_snip_to_bytes(packet, 16, 8));
    long protocolMessageType = packet_to_int(packet_snip_to_bytes(packet, 8, 24));
    long overlayId = packet_to_int(packet_snip_to_bytes(packet, 32, 32));

    // TODO: We should also get the overlay hash id here and compare it to our own
    if (overlayId != ((hc_protocol_shell_t*)(hypercast->protocol))->overlayId) {
        ESP_LOGE(TAG, "Overlay hash does not match this node's hash, discarding packet");
        return;
    }
    
    switch (protocolId) {
        case HC_PROTOCOL_SPT:
            spt_parse(packet, protocolMessageType, overlayId, messageLength, hypercast);
            break;
        default:
            ESP_LOGE(TAG, "MESSAGE FROM UNSUPPORTED PROTOCOL RECEIVED");
            break;
    }
}

void hc_protocol_maintenance(hypercast_t *hypercast) {
    // Each protocol has maintenance that it may need to do automatically at some interval
    // This is its opportunity to do that!
    switch (((hc_protocol_shell_t*)(hypercast->protocol))->id) {
        case HC_PROTOCOL_SPT:
            spt_maintenance(hypercast);
            break;
        default:
            ESP_LOGE(TAG, "HYPERCAST RUNNING ON UNSUPPORTED PROTOCOL");
            break;
    }
    // Once we've maintained, we can also send a node update if enabled
    if (SEND_MEASURES == 1) {
        log_nodestate(hypercast);
    }
    return;
}

void* resolve_protocol_to_install(int config, uint32_t sourceLogicalAddress) { // config type should be more interesting
    // This function will return some void * cast of an allocated protocol object
    // it would also be a switch, but here we're just returning a pre-built spt obj
    void* protocol = (void*)spt_protocol_from_config(sourceLogicalAddress);
    ((hc_protocol_shell_t*)protocol)->id = HC_PROTOCOL_SPT;
    // TODO: Derive Overlay hash properly
    ((hc_protocol_shell_t*)protocol)->overlayId = 1462324117;
    return protocol;
}