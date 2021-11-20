
#include "esp_log.h"

#include "hc_buffer.h"
#include "spt.h"

void spt_parse(hc_packet_t* packet, int messageType, long overlayID, long messageLength) {
    ESP_LOGI(TAG, "Reached SPT Parser");
    // Here we'll check the message type and build the appropriate message
    // Then it will be up to the function passed to at the end of each switch statement to handle that message
    ESP_LOGI(TAG, "Message Type: %d", messageType);
    switch (messageType) {
        case SPT_BEACON_MESSAGE_TYPE:
            ESP_LOGI(TAG, "Received Beacon Message");
            // Now parse all the components of this message
            // This all comes directly from page 27 of SPT spec -> https://www.comm.utoronto.ca/hypercast/material/SPT_Protocol_03-20-05.pdf 
            spt_msg_beacon_t *beaconMessage = malloc(sizeof(spt_msg_beacon_t));
            beaconMessage->senderID = strtol(packet_digest_to_bytes(packet, 32, 64), NULL, 16);
            ESP_LOGI(TAG, "Beacon Message Parsed, senderid is %d", beaconMessage->senderID);
            beaconMessage->physicalAddress = packet_digest_to_bytes(packet, 48, 96);
            beaconMessage->coreID = strtol(packet_digest_to_bytes(packet, 32, 144), NULL, 16);
            beaconMessage->ancestorID = strtol(packet_digest_to_bytes(packet, 32, 176), NULL, 16);
            beaconMessage->cost = strtol(packet_digest_to_bytes(packet, 16, 208), NULL, 16);
            beaconMessage->pathQuality = strtol(packet_digest_to_bytes(packet, 16, 224), NULL, 16);
            beaconMessage->sequenceNumber = strtol(packet_digest_to_bytes(packet, 64, 240), NULL, 16);
            // Now we need to parse the adjacency table
            long tableSize = strtol(packet_digest_to_bytes(packet, 32, 304), NULL, 16);
            ESP_LOGI(TAG, "Adjacencytablelength %ld", tableSize);
            beaconMessage->adjacencyTable = malloc(sizeof(adjacency_table_t));
            beaconMessage->adjacencyTable->size = tableSize;
            beaconMessage->adjacencyTable->entries = malloc(sizeof(adjacency_table_entry_t) * tableSize);
            int startingIndex = 336;
            for (int entry=0; entry<tableSize; entry++) {
                beaconMessage->adjacencyTable->entries[entry] = malloc(sizeof(adjacency_table_entry_t));
                beaconMessage->adjacencyTable->entries[entry]->id = strtol(packet_digest_to_bytes(packet, 32, startingIndex+entry*40), NULL, 16);
                beaconMessage->adjacencyTable->entries[entry]->quality = strtol(packet_digest_to_bytes(packet, 8, startingIndex+entry*40+32), NULL, 16);
            }
            // CHECK
            // Then send it to the handler that acts based on the message information
            break;
        case SPT_GOODBYE_MESSAGE_TYPE:
            ESP_LOGI(TAG, "Received Goodbye Message");
            break;
        case SPT_ROUTE_REQ_MESSAGE_TYPE:
            ESP_LOGI(TAG, "Received Route Request Message");
            break;
        case SPT_ROUTE_REPLY_MESSAGE_TYPE:
            ESP_LOGI(TAG, "Received Route Reply Message");
            break;
        default:
            ESP_LOGE(TAG, "Received Unknown SPT Message Type");
            break;
    }
}