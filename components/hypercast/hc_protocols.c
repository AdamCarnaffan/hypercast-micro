/*
 * This file is designed to handle general work that spans across protocols, then hand the packet off
 * to the appropriate protocol handler. It will then do all of the remaining work to understand what was received,
 * and will then set things up as appropriate
 */
#include "hc_protocols.h"
#include "hc_buffer.h"

// Protocol Includes
#include "spt.h"

static const char* TAG = "HC_PROTOCOLS";

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

    // Make sure the Overlay hashes match
    if (overlayId != ((hc_protocol_shell_t*)(hypercast->protocol))->overlayId) {
        ESP_LOGE(TAG, "Overlay hash does not match this node's hash, discarding packet");
        return;
    }
    
    // Make sure that "MessageLength" is less than or equal to the packet length
    // Otherwise some packet was lost
    if (messageLength > packet->size) {
        ESP_LOGE(TAG, "Message length > packet length, packet dropped");
        return;
    }
    
    switch (protocolId) {
        case HC_PROTOCOL_SPT:
            ESP_LOGI(TAG, "Protocol Packet Received for SPT");
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
    return;
}

void* resolve_protocol_to_install(int config, uint32_t sourceLogicalAddress) { // config type should be more interesting
    // This function will return some void * cast of an allocated protocol object
    // it would also be a switch, but here we're just returning a pre-built spt obj
    void* protocol = (void*)spt_protocol_from_config(sourceLogicalAddress);
    ((hc_protocol_shell_t*)protocol)->id = HC_PROTOCOL_SPT;
    ((hc_protocol_shell_t*)protocol)->overlayId = set_overlay_hash();
    return protocol;
}

bool hc_overlay_sender_trusted(hc_msg_overlay_t* msg, hypercast_t* hypercast) {
    // This is determined by protocol
    switch (((hc_protocol_shell_t*)hypercast->protocol)->id) {
        case HC_PROTOCOL_SPT:
            return spt_overlay_sender_trusted(msg, hypercast);
        default:
            return false;
    }
}