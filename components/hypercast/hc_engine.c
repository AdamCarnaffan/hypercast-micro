/*
* The goal here is that this file will handle the immediate parsing of the overlay and protocol messages
* as much as possible, handing off to the protocol parsers and other functions as soon as necessary.
* This is also where the event that handles reading the receive buffer lives, and where we'll shoot messages
* into the send queue from
*/
#include "hc_engine.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hc_protocols.h"
#include "hc_overlay.h"

static const char* TAG = "HC_ENGINE";

void hc_engine_handler(hypercast_t *hypercast) {
    // Now init and prep for engine
    hc_packet_t *packet = NULL;
    ESP_LOGI(TAG, "Buffer Processor Ready");
    while (1) {
        ESP_LOGI(TAG, "Buffer Processor Running");
        // SEND DISCOVERY
        // First we'll send out our protocol discovery packet if necessary
        // This is where we check the protocol and discovery timings
        // Then use the func to add a protocol discovery packet to the send buffer
        hc_protocol_maintenance(hypercast);

        // READ BUFFER
        // Clear packet saved
        if (packet != NULL) {
            free(packet);
            packet = NULL;
        }

        // Check if anything exists in buffer
        packet = hc_pop_buffer(hypercast->receiveBuffer);
        // If we have NO packet, stop here
        if (packet == NULL) { vTaskDelay(500 / portTICK_PERIOD_MS); continue; }
        // Now we know we have a packet!
        // Parse time :)
        ESP_LOGI(TAG, "Packet Received");
        // First thing to do is a length check.
        // There are only two allowable packet lengths, so lets make sure we meet one
        if (packet->size < HC_OVERLAY_PACKET_LENGTH) {
            ESP_LOGE(TAG, "Packet not readable by Hypercast (Too Short)");
            continue;
        }
        // Now let's first check the HC protocol ID to see if we can handle this message
        long protocolId = packet_to_int(packet_snip_to_bytes(packet, 4, 0)); // It's only the first byte
        ESP_LOGI(TAG, "Protocol ID: %ld", protocolId);
        // We can only handle 13 which is an overlay message, or a protocol message
        if (protocolId == HC_PROTOCOL_OVERLAY_MESSAGE) {
            // Send to forwarding engine
            ESP_LOGI(TAG, "Sending to forwarding engine");
            hc_forward(packet, hypercast);
        } else {
            // Send to protocol parser
            ESP_LOGI(TAG, "Sending to protocol parser");
            hc_protocol_parse(packet, protocolId, hypercast);
        }
    }
}

void hc_forward(hc_packet_t *packet, hypercast_t *hypercast) {
    // First we'll read the packet to interpret the message & receive it
    // We're assuming that all overlay messages are multicast, but we check datamode anyway
    hc_msg_overlay_t *msg = hc_msg_overlay_parse(packet);

    if (msg == NULL) {
        ESP_LOGE(TAG, "Failed to parse overlay message");
        return;
    }

    // Then check if this message is from a member of our ?neighbor? table
    // If it is, then continue, otherwise, stop
    if (hc_overlay_sender_trusted(msg, hypercast) == false) {
        ESP_LOGD(TAG, "Sender not trusted - bouncing message");
        return;
    }

    // Before taking any action, check for a route record table
    // If we're on it, drop the message
    if (hc_overlay_route_record_contains(msg, hypercast->senderTable->sourceAddressLogical) == 1) {
        ESP_LOGI(TAG, "Dropping message from %d because we're on the route record table", msg->sourceLogicalAddress);
        hc_msg_overlay_free(msg);
        return;
    }
    
    // Once we've read the packet, we need to send it forward!
    // Before forwarding, tick down the hop limit and add to the last hop logical
    msg->hopLimit = msg->hopLimit - 1;
    msg->previousHopLogicalAddress = hypercast->senderTable->sourceAddressLogical;
    // We'll also append ourselves to the route record (creating one if the other node was negligent)
    hc_overlay_route_record_append(msg, hypercast->senderTable->sourceAddressLogical);

    // Then send it out! (forwarding part)
    hc_packet_t *forwardPacket = hc_msg_overlay_encode(msg);
    hc_push_buffer(hypercast->sendBuffer, forwardPacket->data, forwardPacket->size);
    // The packet data has been passed to the buffer, so we can cleanup the packet
    free(forwardPacket); // (We keep the data allocated because the buffer will see that cleaned up)
    // Then we need to run our api callback on the payload :)
    char* callbackData;
    int callbackDataLength;
    hc_msg_overlay_get_primary_payload(msg, &callbackData, &callbackDataLength);
    hypercast->callback(callbackData, callbackDataLength);
    // Then free the message data
    hc_msg_overlay_free(msg);
}