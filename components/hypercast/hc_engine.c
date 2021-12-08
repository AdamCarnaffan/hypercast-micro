/*
* The goal here is that this file will handle the immediate parsing of the overlay and protocol messages
* as much as possible, handing off to the protocol parsers and other functions as soon as necessary.
* This is also where the event that handles reading the receive buffer lives, and where we'll shoot messages
* into the send queue from
*/
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hc_buffer.h"
#include "hc_engine.h"
#include "hc_protocols.h"

void hc_engine_handler(void *pvParameters) {

    // pvParameters in this case is access to our buffer
    hc_buffer_t* buffer = (hc_buffer_t *)pvParameters;

    // Now init and prep for engine
    hc_packet_t *packet;
    ESP_LOGI(TAG, "Buffer Processor Ready");
    while (1) {
        // Clear packet saved
        packet = NULL;
        // Check if anything exists in buffer
        packet = hc_pop_buffer(buffer);
        // If we have NO packet, stop here
        // ESP_LOGI(TAG, "IN HERE length %d", buffer.current_size);
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
        } else {
            // Send to protocol parser
            ESP_LOGI(TAG, "Sending to protocol parser");
            hc_protocol_parse(packet, protocolId);
        }
    }
}