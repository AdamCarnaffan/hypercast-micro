/*
* The goal here is that this file will handle the immediate parsing of the overlay and protocol messages
* as much as possible, handing off to the protocol parsers and other functions as soon as necessary.
* This is also where the event that handles reading the receive buffer lives, and where we'll shoot messages
* into the send queue from
*/
#include "esp_log.h"

#include "hc_buffer.h"

#ifndef TAG
#define TAG "HC_ENGINE"
#endif

void hc_engine_handler(void *pvParameters) {

    // pvParameters in this case is access to our buffer
    hc_buffer_t buffer = *((hc_buffer_t *)pvParameters);

    // Now init and prep for engine
    hc_packet_t *packet;

    while (1) {
        // Clear packet saved
        packet = NULL;
        // Check if anything exists in buffer
        packet = hc_pop_buffer(&buffer);
        // If we have NO packet, stop here
        if (packet == NULL) { continue; }
        // Now we know we have a packet!
        // Parse time :)
        ESP_LOGI(TAG, "%s", packet->data);
        // First thing to do is a length check.
        // There are only two allowable packet lengths, so lets make sure we have one of them

    }
}