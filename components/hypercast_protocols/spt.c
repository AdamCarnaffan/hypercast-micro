
#include "esp_log.h"
#include <inttypes.h>

#include "hc_buffer.h"
#include "spt.h"

void spt_parse(hc_packet_t* packet, int messageType, long overlayID, long messageLength) {
    ESP_LOGI(TAG, "Reached SPT Parser");
    // Here we'll check the message type and build the appropriate message
    // Then it will be up to the function passed to at the end of each switch statement to handle that message
    // This all comes directly from page 27 of SPT spec -> https://www.comm.utoronto.ca/hypercast/material/SPT_Protocol_03-20-05.pdf 
    // SPT doc page 6 has information on algorithms used to calculate costing (there are options)
    // Sender data package is updated as seen on page 82 of v4 spec -> https://www.comm.utoronto.ca/~jorg/archive/papers/Majidthesis.pdf
    int i; // We'll need an iterator a few times
    int startingIndex; // We'll also need an index to begin an iterator at a few times
    // Metric for adjacency should be least hops with at least a minimum link quality
    // Maybe use RSSI? But code is meant not to be specific to wireless (and RSSI may not be standardized?)
    ESP_LOGI(TAG, "Message Type: %d", messageType);
    // Bit offset includes message type, message length, protocol message type, and overlay ID (8 bytes)
    int bitOffset = 64; // bits that come before the protocol message format listed (already ready)
    switch (messageType) {
        case SPT_BEACON_MESSAGE_TYPE:
            ESP_LOGI(TAG, "Received Beacon Message");
            // Now parse all the components of this message
            spt_msg_beacon_t *beaconMessage = malloc(sizeof(spt_msg_beacon_t));
            // NOTE: We've already read the first 5 bytes (common to all protocol messages)
            // These 5 are AFTER the 2 bytes read for message length
            // Start by resolving the sender table
            beaconMessage->senderTable = malloc(sizeof(sender_table_t));
            // In normal SPT, this has to be 1
            long senderCount = 1;
            beaconMessage->senderTable->interfaceCount = senderCount;
            beaconMessage->senderTable->entries = malloc(sizeof(sender_table_entry_t*) * senderCount);
            startingIndex = 16 + bitOffset;
            for (i=0; i<senderCount; i++) {
                // Theoretically this loops in CSA, but normal SPT only sees 1 iteration
                // for each entry, we'll allocate then populate
                beaconMessage->senderTable->entries[i] = malloc(sizeof(sender_table_entry_t));
                // Now add the type of the address as IPv4 (assumed)
                beaconMessage->senderTable->entries[i]->type = 1;
                // Then add packet data
                beaconMessage->senderTable->entries[i]->hash = packet_to_int(packet_snip_to_bytes(packet, 16, startingIndex));
                long addressLength = packet_to_int(packet_snip_to_bytes(packet, 8, startingIndex + 16));
                // Now add the address
                beaconMessage->senderTable->entries[i]->address = malloc(sizeof(char)*(addressLength-2));
                beaconMessage->senderTable->entries[i]->addressLength = addressLength;
                beaconMessage->senderTable->entries[i]->address = (char *)(packet_snip_to_bytes(packet, (addressLength-2)*8, startingIndex + 24));
                // Then the last 2 bytes are the port
                beaconMessage->senderTable->entries[i]->port = packet_to_int(packet_snip_to_bytes(packet, 16, startingIndex + 24 + (addressLength-2)*8));
                startingIndex += 3*8 + addressLength*8; // We update this index because each interface is dynamically sized
            }
            // Finish the sentertable by adding the source logical as well
            beaconMessage->senderTable->sourceAddressLogical = packet_to_int(packet_snip_to_bytes(packet, 32, startingIndex));
            // HERE WE RE-GROUND THE BITOFFSET!!!!
            bitOffset = startingIndex + 32;
            // NOW START FROM 0 WITH THE REST OF THE DATA!!! THIS IS IMPORTANT
            beaconMessage->rootAddressLogical = packet_to_int(packet_snip_to_bytes(packet, 32, bitOffset));
            beaconMessage->parentAddressLogical = packet_to_int(packet_snip_to_bytes(packet, 32, bitOffset + 32));
            beaconMessage->cost = packet_to_int(packet_snip_to_bytes(packet, 32, bitOffset + 64));
            beaconMessage->timestamp = packet_to_int(packet_snip_to_bytes(packet, 64, bitOffset + 96));
            ESP_LOGI(TAG, "Beacon Message Parsed, timestamp is %" PRIu64 "", beaconMessage->timestamp);
            // Now we need to parse the adjacency table
            long tableSize = packet_to_int(packet_snip_to_bytes(packet, 32, bitOffset + 160));
            // ESP_LOGI(TAG, "Adjacencytablelength %d", tableSize);
            beaconMessage->adjacencyTable = malloc(sizeof(adjacency_table_t));
            beaconMessage->adjacencyTable->size = tableSize;
            beaconMessage->adjacencyTable->entries = malloc(sizeof(adjacency_table_entry_t*) * tableSize);
            startingIndex = 312 + bitOffset;
            for (i=0; i<tableSize; i++) {
                beaconMessage->adjacencyTable->entries[i] = malloc(sizeof(adjacency_table_entry_t));
                beaconMessage->adjacencyTable->entries[i]->id = packet_to_int(packet_snip_to_bytes(packet, 32, startingIndex+i*40));
                beaconMessage->adjacencyTable->entries[i]->quality = packet_to_int(packet_snip_to_bytes(packet, 8, startingIndex+i*40+32));
            }
            // Then send it to the handler that acts based on the message information
            // try just always sending it back?
            

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