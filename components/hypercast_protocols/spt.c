
#include "esp_log.h"
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "hc_buffer.h"
#include "spt.h"

void spt_parse(hc_packet_t* packet, int messageType, long overlayID, long messageLength, hypercast_t* hypercast) {
    ESP_LOGI(TAG, "Reached SPT Parser");
    // Here we'll check the message type and build the appropriate message
    // Then it will be up to the function passed to at the end of each switch statement to handle that message
    // This all comes directly from page 27 of SPT spec -> https://www.comm.utoronto.ca/hypercast/material/SPT_Protocol_03-20-05.pdf 
    // SPT doc page 6 has information on algorithms used to calculate costing (there are options)
    // Sender data packet is updated as seen on page 82 of v4 spec -> https://www.comm.utoronto.ca/~jorg/archive/papers/Majidthesis.pdf
    int i; // We'll need an iterator a few times
    int startingIndex; // We'll also need an index to begin an iterator at a few times
    long senderCount; // Used to count up the senders in a senderTable (used in hello and goodbye)
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
            beaconMessage->senderTable = malloc(sizeof(hc_sender_table_t));
            // In normal SPT, this has to be 1
            senderCount = 1;
            beaconMessage->senderTable->size = senderCount;
            beaconMessage->senderTable->entries = malloc(sizeof(hc_sender_entry_t*) * senderCount);
            startingIndex = 16 + bitOffset;
            for (i=0; i<senderCount; i++) {
                // Theoretically this loops in CSA, but normal SPT only sees 1 iteration
                // for each entry, we'll allocate then populate
                beaconMessage->senderTable->entries[i] = malloc(sizeof(hc_sender_entry_t));
                // Now add the type of the address as IPv4 (assumed)
                beaconMessage->senderTable->entries[i]->type = 1;
                // Then add packet data
                beaconMessage->senderTable->entries[i]->hash = packet_to_int(packet_snip_to_bytes(packet, 16, startingIndex));
                long addressLength = packet_to_int(packet_snip_to_bytes(packet, 8, startingIndex + 16));
                // Now add the address
                beaconMessage->senderTable->entries[i]->address = malloc(sizeof(hc_ipv4_addr_t));
                beaconMessage->senderTable->entries[i]->addressLength = addressLength;
                // The first 4 (address length needs to be 6 or I panic) are the address bits
                if (addressLength != 6) {
                    ESP_LOGE(TAG, "Address length is not 6, but %d. I can't deal with that", (int)addressLength);
                    return;
                }
                // Populate address
                for (int j=0; j<addressLength-2; j++) {
                    beaconMessage->senderTable->entries[i]->address->addr[j] = (uint8_t)packet_to_int(packet_snip_to_bytes(packet, 8, startingIndex + 24 + (j*8)));
                }
                // Then the last 2 bytes are the port
                beaconMessage->senderTable->entries[i]->port = packet_to_int(packet_snip_to_bytes(packet, 16, startingIndex + 24 + (addressLength-2)*8));
                startingIndex += 3*8 + addressLength*8; // We update this index because each interface is dynamically sized
            }
            // Finish the sendertable by adding the source logical as well
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
            long tableSize = packet_to_int(packet_snip_to_bytes(packet, 32, bitOffset + 160)); // "Sender Count"
            // ESP_LOGI(TAG, "Adjacencytablelength %d", tableSize);
            beaconMessage->adjacencyTable = malloc(sizeof(adjacency_table_t));
            beaconMessage->adjacencyTable->size = tableSize;
            beaconMessage->adjacencyTable->entries = malloc(sizeof(adjacency_table_entry_t*) * tableSize);
            startingIndex = bitOffset + 160;
            for (i=0; i<tableSize; i++) {
                beaconMessage->adjacencyTable->entries[i] = malloc(sizeof(adjacency_table_entry_t));
                beaconMessage->adjacencyTable->entries[i]->id = packet_to_int(packet_snip_to_bytes(packet, 32, startingIndex+i*40));
                beaconMessage->adjacencyTable->entries[i]->quality = packet_to_int(packet_snip_to_bytes(packet, 8, startingIndex+i*40+32));
            }
            // Then send it to the handler that acts based on the message information
            bitOffset = startingIndex + tableSize*40;
            // Reliability is last!
            beaconMessage->reliability = packet_to_int(packet_snip_to_bytes(packet, 16, bitOffset));
            // Then send it to the handler that acts based on the message information
            
            break;
        case SPT_GOODBYE_MESSAGE_TYPE:
            ESP_LOGI(TAG, "Received Goodbye Message");
            // Now parse all the components of this message
            spt_msg_beacon_t *goodbyeMessage = malloc(sizeof(spt_msg_beacon_t));
            // This one's pretty easy because we actually only have the sender table to parse lol
            // NOTE: We've already read the first 5 bytes (common to all protocol messages)
            // These 5 are AFTER the 2 bytes read for message length
            // Start by resolving the sender table
            goodbyeMessage->senderTable = malloc(sizeof(hc_sender_table_t));
            // In normal SPT, this has to be 1
            senderCount = 1;
            goodbyeMessage->senderTable->size = senderCount;
            goodbyeMessage->senderTable->entries = malloc(sizeof(hc_sender_entry_t*) * senderCount);
            startingIndex = 16 + bitOffset;
            for (i=0; i<senderCount; i++) {
                // Theoretically this loops in CSA, but normal SPT only sees 1 iteration
                // for each entry, we'll allocate then populate
                goodbyeMessage->senderTable->entries[i] = malloc(sizeof(hc_sender_entry_t));
                // Now add the type of the address as IPv4 (assumed)
                goodbyeMessage->senderTable->entries[i]->type = 1;
                // Then add packet data
                goodbyeMessage->senderTable->entries[i]->hash = packet_to_int(packet_snip_to_bytes(packet, 16, startingIndex));
                long addressLength = packet_to_int(packet_snip_to_bytes(packet, 8, startingIndex + 16));
                // Now add the address
                goodbyeMessage->senderTable->entries[i]->address = malloc(sizeof(hc_ipv4_addr_t));
                goodbyeMessage->senderTable->entries[i]->addressLength = addressLength;
                // The first 4 (address length needs to be 6 or I panic) are the address bits
                if (addressLength != 6) {
                    ESP_LOGE(TAG, "Address length is not 6, but %d. I can't deal with that", (int)addressLength);
                    return;
                }
                // Populate address
                for (int j=0; j<addressLength-2; j++) {
                    goodbyeMessage->senderTable->entries[i]->address->addr[j] = (uint8_t)packet_to_int(packet_snip_to_bytes(packet, 8, startingIndex + 24 + (j*8)));
                }
                // Then the last 2 bytes are the port
                goodbyeMessage->senderTable->entries[i]->port = packet_to_int(packet_snip_to_bytes(packet, 16, startingIndex + 24 + (addressLength-2)*8));
                startingIndex += 3*8 + addressLength*8; // We update this index because each interface is dynamically sized
            }
            // Finish the sendertable by adding the source logical as well
            goodbyeMessage->senderTable->sourceAddressLogical = packet_to_int(packet_snip_to_bytes(packet, 32, startingIndex));
            // And now we're done
            // Then send it to the handler that acts based on the message information
            
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
    // NOTE: may want to consider a return here instead of calling hypercast to act?
}

hc_packet_t* spt_encode(void *msg, int messageType, hypercast_t *hypercast) {
    // Fetch Protocol Data
    protocol_spt *spt = (protocol_spt*)hypercast->protocol;
    // Define data cache
    char data[HC_BUFFER_DATA_MAX]; // Temporary buffer of max size to shove data into
    int dataSize = 0;
    // we gotta bundle it up in here
    // First there are general bits we can throw on the front
    write_bytes(data, 3, 4, 0, HC_BUFFER_DATA_MAX); // Protocol Number
    write_bytes(data, 3, 4, 4, HC_BUFFER_DATA_MAX); // Protocol Version? I'm not sure what this is <<HELP>>
    // We also leave a space here for the length of the message (16 bits)
    // Then setup to write
    int bitOffset = 8 + 16; // Protocol + message length
    int i; // Iterator :)
    // this switch will handle each messageType case
    switch (messageType) {
        case SPT_BEACON_MESSAGE_TYPE:
            // First the basics (kinda obvious)
            write_bytes(data, SPT_BEACON_MESSAGE_TYPE, 8, bitOffset, HC_BUFFER_DATA_MAX); // Message Type
            write_bytes(data, 1462324117, 32, bitOffset + 8, HC_BUFFER_DATA_MAX); // Overlay Hash ID <<HELP>> (Derivable?)
            spt_msg_beacon_t *message = (spt_msg_beacon_t*)msg;
            // Now we'll read through the message and add it to the packet
            // First the sender table
            // Not sure at all where the number of interfaces goes... <<HELP>> (16 bits??)
            // BAD: To make this work, insert ff41 into the data buffer before the first interface
            write_bytes(data, 0xff41, 16, bitOffset + 40, HC_BUFFER_DATA_MAX); // Number of interfaces
            bitOffset = bitOffset + 40 + 16; // We'll start at the beginning of the sender table
            for (i=0; i<hypercast->senderTable->size; i++) {
                // First the type
                // write_bytes(data, message->senderTable->entries[i]->type, 8, bitOffset, HC_BUFFER_DATA_MAX);
                // Then the hash
                write_bytes(data, message->senderTable->entries[i]->hash, 16, bitOffset, HC_BUFFER_DATA_MAX);
                // Then the address length
                write_bytes(data, message->senderTable->entries[i]->addressLength, 8, bitOffset + 16, HC_BUFFER_DATA_MAX);
                // Then the address
                for (int j=0; j<message->senderTable->entries[i]->addressLength-2; j++) { // 2 are for the port
                    write_bytes(data, message->senderTable->entries[i]->address->addr[j], 8, bitOffset + 24 + j*8, HC_BUFFER_DATA_MAX);
                }
                bitOffset += (message->senderTable->entries[i]->addressLength-2)*8 + 24; // Most of the offset update
                // Then the port
                write_bytes(data, message->senderTable->entries[i]->port, 16, bitOffset, HC_BUFFER_DATA_MAX);
                bitOffset += 16; // Finish offset update
            }
            // Next is the sourceAddressLogical
            write_bytes(data, spt->treeInfoTable->id, 32, bitOffset, HC_BUFFER_DATA_MAX); // message->senderTable->sourceLogicalAddress
            bitOffset += 32;
            // Now move on to the beacon message data with offset reset
            write_bytes(data, spt->treeInfoTable->id, 32, bitOffset, HC_BUFFER_DATA_MAX); // message->rootLogicalAdddress
            write_bytes(data, message->parentAddressLogical, 32, bitOffset + 32, HC_BUFFER_DATA_MAX);
            write_bytes(data, message->cost, 32, bitOffset + 64, HC_BUFFER_DATA_MAX);
            write_bytes(data, message->timestamp, 64, bitOffset + 96, HC_BUFFER_DATA_MAX);
            // Then we finish with the adjacency table
            // We'll send a 0 sender count here
            // For now this is ALWAYS BLANK <<HELP>>
            write_bytes(data, 0, 32, bitOffset + 160, HC_BUFFER_DATA_MAX);
            // TODO: ADD ADJACENCY TABLE
            bitOffset += 160 + 32; // + adjacency table size entries of 40 bits
            // And the reliability is last
            write_bytes(data, message->reliability, 16, bitOffset, HC_BUFFER_DATA_MAX);
            bitOffset += 16; // Just to maintain it to the end :)
            dataSize = bitOffset / 8;
            break;
        case SPT_GOODBYE_MESSAGE_TYPE:
            break;
        case SPT_ROUTE_REQ_MESSAGE_TYPE:
            ESP_LOGE(TAG, "SPT does not support Route Requesting at the moment, sorry!");
            break;
        case SPT_ROUTE_REPLY_MESSAGE_TYPE:
            ESP_LOGE(TAG, "SPT does not support Route Replying at the moment, sorry!");
            break;
        default:
            ESP_LOGE(TAG, "Unknown SPT Message Type");
            return NULL;
    }
    // In all cases the last thing to write is the length of the message!
    write_bytes(data, dataSize-3, 16, 8, HC_BUFFER_DATA_MAX);
    // Now at the end let's pretty it up!
    hc_packet_t *packet = malloc(sizeof(hc_packet_t));
    packet->size = dataSize;
    packet->data = malloc(sizeof(char)*dataSize);
    memcpy(packet->data, data, dataSize);
    return packet;
}

protocol_spt* spt_protocol_from_config() {
    srand(time(NULL));
    protocol_spt* spt;
    spt = malloc(sizeof(protocol_spt));
    spt->id = rand() % 999; // Not sure we need this
    spt->lastBeacon = 0; // Never sent, so diff will be massive!
    spt->heartbeatTime = 3;

    // Init tables

    // TREE INFO
    spt->treeInfoTable = malloc(sizeof(pt_spt_tree_info_table_t));
    spt->treeInfoTable->id = spt->id;
    spt->treeInfoTable->physicalAddress = 0;
    spt->treeInfoTable->coreId = 0;
    spt->treeInfoTable->ancestorId = 0;
    spt->treeInfoTable->cost = 0;
    spt->treeInfoTable->pathMetric = 0;
    spt->treeInfoTable->sequenceNumber = 0;

    // NEIGHBORHOOD
    spt->neighborhoodTable = malloc(sizeof(pt_spt_neighborhood_table_t));
    spt->neighborhoodTable->size = 0;

    // BACKUP ANCESTORS
    spt->backupAncestorTable = malloc(sizeof(pt_spt_backup_ancestor_table_t));
    spt->backupAncestorTable->size = 0;

    // ADJACENCY
    spt->adjacencyTable = malloc(sizeof(pt_spt_adjacency_table_t));
    spt->adjacencyTable->size = 0;

    // CORE TABLE
    spt->coreTable = malloc(sizeof(pt_spt_core_table_t));
    spt->coreTable->size = 0;
    spt->coreTable->lastUpdate = 0;

    // Now return built protocol
    return spt;
}

void spt_maintenance(hypercast_t* hypercast) {
    // SPT maintenance consists of sending a beacon message with
    // the protocol's current state information
    // We'll do that here, but it's a periodic task, so we'll only
    // do it when necessary

    // Load necessary values
    unsigned currentTime = (unsigned)time(NULL);
    protocol_spt* spt = (protocol_spt*)hypercast->protocol;

    // First check necessity of maintenance
    if (currentTime - spt->lastBeacon < spt->heartbeatTime) {
        return;
    }

    // Now execute
    ESP_LOGI(TAG, "Time to Maintain SPT");

    // 1. Generate beacon message base
    spt_msg_beacon_t *beaconMessage = malloc(sizeof(spt_msg_beacon_t));
    // 2. Populate state information
    // Sender table with 1 entry (because we only use 1 interface)
    beaconMessage->senderTable = hypercast->senderTable;
    // Misc data
    beaconMessage->rootAddressLogical = spt->id;
    beaconMessage->parentAddressLogical = 0; // Would be read from the neighbors table if this were populating
    beaconMessage->cost = 0;
    beaconMessage->timestamp = currentTime; // Needs to be real epoch timestamp
    beaconMessage->senderCount = 1; // Default for new beacon I think???
    beaconMessage->reliability = 10000; // No idea where this comes from, but mimic HC's value for now
    // Adjacency Table
    beaconMessage->adjacencyTable = malloc(sizeof(adjacency_table_t));
    beaconMessage->adjacencyTable->size = spt->adjacencyTable->size;
    beaconMessage->adjacencyTable->entries = malloc(sizeof(adjacency_table_entry_t*) * spt->adjacencyTable->size);
    for (int i=0; i<spt->adjacencyTable->size; i++) {
        beaconMessage->adjacencyTable->entries[i] = malloc(sizeof(adjacency_table_entry_t));
        beaconMessage->adjacencyTable->entries[i]->id = spt->adjacencyTable->entries[i]->id;
        beaconMessage->adjacencyTable->entries[i]->quality = spt->adjacencyTable->entries[i]->quality;
    }
    // 3. Encode it
    hc_packet_t *packet = spt_encode(beaconMessage, SPT_BEACON_MESSAGE_TYPE, hypercast);
    // 4. Send it off
    hc_push_buffer(hypercast->sendBuffer, packet->data, packet->size);
    // 5. Update last beacon time
    spt->lastBeacon = currentTime;
}