
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "spt.h"
#include "hc_protocols.h"
#include "hc_buffer.h"
#include "hc_lib.h"
#include "hc_overlay.h"

static const char* TAG = "HC_PROTOCOL_SPT";

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
            // They deal in ms out there, but we're gonna keep it to seconds over here
            beaconMessage->timestamp /= 1000;
            ESP_LOGI(TAG, "Beacon Message Parsed, timestamp is %" PRIu64 "", beaconMessage->timestamp);
            // Now we need to parse the adjacency table
            uint32_t tableSize = packet_to_int(packet_snip_to_bytes(packet, 32, bitOffset + 160)); // "Sender Count"
            ESP_LOGI(TAG, "%s", packet_snip_to_bytes(packet, 32, bitOffset + 160)->data);
            beaconMessage->adjacencyTable = malloc(sizeof(adjacency_table_t));
            beaconMessage->adjacencyTable->size = tableSize;
            beaconMessage->adjacencyTable->entries = malloc(sizeof(adjacency_table_entry_t*) * tableSize);
            ESP_LOGI(TAG, "Table size is %u, found at %d", tableSize, bitOffset + 160);
            startingIndex = bitOffset + 192;
            for (i=0; i<tableSize; i++) {
                beaconMessage->adjacencyTable->entries[i] = malloc(sizeof(adjacency_table_entry_t));
                beaconMessage->adjacencyTable->entries[i]->id = packet_to_int(packet_snip_to_bytes(packet, 32, startingIndex+i*40));
                beaconMessage->adjacencyTable->entries[i]->quality = packet_to_int(packet_snip_to_bytes(packet, 8, startingIndex+(i*40)+32));
                // Now we need to do an & operation on "quality" because it actually only occupies bits 2-8 ( & 0x7F )
                beaconMessage->adjacencyTable->entries[i]->quality = beaconMessage->adjacencyTable->entries[i]->quality & 0x7F;
            }
            // Then send it to the handler that acts based on the message information
            bitOffset = startingIndex + tableSize*40;
            // Reliability is last!
            beaconMessage->reliability = packet_to_int(packet_snip_to_bytes(packet, 16, bitOffset));
            // Then send it to the handler that acts based on the message information
            spt_handle_beacon_message(beaconMessage, hypercast);
            spt_free_beacon_message(beaconMessage);
            break;
        case SPT_GOODBYE_MESSAGE_TYPE:
            ESP_LOGI(TAG, "Received Goodbye Message");
            // Now parse all the components of this message
            spt_msg_goodbye_t *goodbyeMessage = malloc(sizeof(spt_msg_goodbye_t));
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
            spt_handle_goodbye_message(goodbyeMessage, hypercast);
            spt_free_goodbye_message(goodbyeMessage);
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
            write_bytes(data, spt->overlayId, 32, bitOffset + 8, HC_BUFFER_DATA_MAX); // Overlay Hash ID <<HELP>> (Derivable?)
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
            write_bytes(data, message->timestamp*1000, 64, bitOffset + 96, HC_BUFFER_DATA_MAX); // *1000 because they use ms out there
            // Then we finish with the adjacency table
            write_bytes(data, spt->adjacencyTable->size, 32, bitOffset + 160, HC_BUFFER_DATA_MAX);
            bitOffset += 160 + 32; // + adjacency table size entries of 40 bits
            // Now we can add the actual adjacency table entries
            for (i=0;i<spt->adjacencyTable->size;i++) {
                write_bytes(data, spt->adjacencyTable->entries[i]->id, 32, bitOffset, HC_BUFFER_DATA_MAX);
                write_bytes(data, spt->adjacencyTable->entries[i]->quality, 8, bitOffset + 32, HC_BUFFER_DATA_MAX);
                bitOffset += 40; // Size of an adjacency table entry
            }
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

protocol_spt* spt_protocol_from_config(uint32_t sourceLogicalAddress) {
    protocol_spt* spt;
    spt = malloc(sizeof(protocol_spt));
    spt->id = HC_PROTOCOL_SPT; // Not sure we need this
    spt->lastBeacon = 0; // Never sent, so diff will be massive!
    spt->heartbeatTime = 5;

    // Init tables

    // TREE INFO
    spt->treeInfoTable = malloc(sizeof(pt_spt_tree_info_table_t));
    spt->treeInfoTable->id = sourceLogicalAddress;
    spt->treeInfoTable->physicalAddress = sourceLogicalAddress;
    spt->treeInfoTable->rootId = 0;
    spt->treeInfoTable->ancestorId = 0;
    spt->treeInfoTable->cost = 0;
    spt->treeInfoTable->pathMetric = spt_pathmetric_minimumcost(NULL);
    spt->treeInfoTable->sequenceNumber = 0;

    // NEIGHBORHOOD
    spt->neighborhoodTable = malloc(sizeof(pt_spt_neighborhood_table_t));
    spt->neighborhoodTable->entries = malloc(sizeof(pt_spt_neighborhood_entry_t*)*SPT_TABLE_NEIGHBORHOOD_MAX_SIZE);
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
    uint64_t currentTime = get_epoch();
    protocol_spt* spt = (protocol_spt*)hypercast->protocol;

    // First check necessity of maintenance
    if (currentTime - spt->lastBeacon < spt->heartbeatTime) {
        return;
    }
    
    // Setup iterator
    int i;

    // Now execute
    ESP_LOGI(TAG, "Maintaining SPT");

    // 1. Generate beacon message base
    spt_msg_beacon_t *beaconMessage = malloc(sizeof(spt_msg_beacon_t));
    // 2. Populate state information
    // Sender table with 1 entry (because we only use 1 interface)
    // We need to copy it here so that the beacon free can free it later!
    beaconMessage->senderTable = malloc(sizeof(hc_sender_table_t));
    beaconMessage->senderTable->size = hypercast->senderTable->size;
    beaconMessage->senderTable->entries = malloc(sizeof(hc_sender_entry_t*)*beaconMessage->senderTable->size);
    for (int i=0;i<beaconMessage->senderTable->size;i++) {
        // Allocation is unnecessary here
        beaconMessage->senderTable->entries[i] = malloc(sizeof(hc_sender_entry_t));
        beaconMessage->senderTable->entries[i]->type = hypercast->senderTable->entries[i]->type;
        beaconMessage->senderTable->entries[i]->addressLength = hypercast->senderTable->entries[i]->addressLength;
        beaconMessage->senderTable->entries[i]->address = hypercast->senderTable->entries[i]->address;
        beaconMessage->senderTable->entries[i]->port = hypercast->senderTable->entries[i]->port;
    }
    beaconMessage->senderTable->sourceAddressLogical = hypercast->senderTable->sourceAddressLogical;
    // Misc data
    beaconMessage->rootAddressLogical = spt->id;
    beaconMessage->parentAddressLogical = spt->treeInfoTable->ancestorId;
    beaconMessage->cost = spt->treeInfoTable->cost;
    beaconMessage->timestamp = currentTime; // Needs to be real epoch timestamp
    beaconMessage->senderCount = spt->adjacencyTable->size; // Default for new beacon I think???
    beaconMessage->reliability = spt_pathmetric_minimumcost(NULL);
    // Adjacency Table
    beaconMessage->adjacencyTable = malloc(sizeof(adjacency_table_t));
    beaconMessage->adjacencyTable->size = spt->adjacencyTable->size;
    beaconMessage->adjacencyTable->entries = malloc(sizeof(adjacency_table_entry_t*) * spt->adjacencyTable->size);
    for (i=0; i<spt->adjacencyTable->size; i++) {
        // Allocation is unnecessary here
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
    // 6. Free memory
    spt_free_beacon_message(beaconMessage);

    // First we'll timeout the adjacency entries
    if (spt->adjacencyTable->size > 0) {
        for (i=0; i<spt->adjacencyTable->size; i++) {
            if (spt->adjacencyTable->entries[i]->timestamp + SPT_ADJACENCY_TIMEOUT < currentTime) {
                // Then we have a node that has timed out
                // We'll remove it from the adjacency table
                // And we'll set i back by one because we've moved table entries to fill this index again
                pt_spt_adjacency_entry_t *entry = spt->adjacencyTable->entries[i];
                for (int j=i; j<spt->adjacencyTable->size; j++) {
                    spt->adjacencyTable->entries[j] = spt->adjacencyTable->entries[j+1];
                }
                spt->adjacencyTable->size--;
                ESP_LOGI(TAG, "Timeout Mechanism has detected that a node left the network");
                // Now we'll free the entry
                free(entry);
                i--;
            }
        }
    }

    // Now we'll timeout the neighborhood entries
    if (spt->neighborhoodTable->size > 0) {
        for (i=0; i<spt->neighborhoodTable->size; i++) {
            if (spt->neighborhoodTable->entries[i]->timestamp + SPT_NEIGHBOR_TIMEOUT < currentTime) {
                // Then we have a node that has timed out
                // We'll remove it from the neighborhood table
                // And we'll set i back by one because we've moved table entries to fill this index again
                pt_spt_neighborhood_entry_t *entry = spt->neighborhoodTable->entries[i];
                for (int j=i; j<spt->neighborhoodTable->size; j++) {
                    spt->neighborhoodTable->entries[j] = spt->neighborhoodTable->entries[j+1];
                }
                spt->neighborhoodTable->size--;
                // We have to do a bit more work if this was an ancestor entry
                if (entry->isAncestor) {
                    // Do reset because we're no longer connected to our ancestor
                    spt->treeInfoTable->ancestorId = spt->treeInfoTable->id;
                    spt->treeInfoTable->rootId = spt->treeInfoTable->id;
                    spt->treeInfoTable->cost = 0;
                    spt->treeInfoTable->pathMetric = spt_pathmetric_minimumcost(NULL);
                }
                ESP_LOGI(TAG, "Timeout Mechanism has detected that a node left the neighborhood");
                // Now we'll free the entry
                free(entry);
                i--;
            }
        }
    }

    // TEMP: Whenever we finish maintenance, send an overlay message out!
    char payload[] = "Hello World from ESP32!";
    hc_msg_overlay_t *overlayMessage = hc_msg_overlay_init_with_payload(hypercast, payload, strlen(payload));
    // Then encode it
    hc_packet_t *packet2 = hc_msg_overlay_encode(overlayMessage);
    // Then send it off
    hc_push_buffer(hypercast->sendBuffer, packet2->data, packet2->size);
    // Then free the message
    free(packet2);
    hc_msg_overlay_free(overlayMessage);

    ESP_LOGI(TAG, "SPT Maintenance Finished");
}

// Message Type Handlers (For Hypercast Updates to State)

void spt_handle_beacon_message(spt_msg_beacon_t* msg, hypercast_t* hypercast) {
    // This section is a replication of the logic found in the SPT protocol manual
    // at https://www.comm.utoronto.ca/hypercast/material/SPT_Protocol_03-20-05.pdf on pages 18-20

    // Set up globals
    int i;
    protocol_spt* spt = (protocol_spt*)hypercast->protocol;

    // Once we've received a message from anywhere, use it to update the local clock time
    if (get_epoch() < HC_FIXED_TIME_MIN_VALUE + 80000) {
        // We need to set the time to something reasonable
        set_epoch(msg->timestamp);
    }

    // 1. Update Adjacency Table

    // First find entry of table
    pt_spt_adjacency_entry_t *adjEntry = NULL;
    for (i=0;i<spt->adjacencyTable->size;i++) {
        if (spt->adjacencyTable->entries[i]->id == msg->senderTable->sourceAddressLogical) {
            adjEntry = spt->adjacencyTable->entries[i];
            break;
        }
    }

    // If we didn't find it, add it
    if (adjEntry == NULL) {
        adjEntry = malloc(sizeof(pt_spt_adjacency_entry_t));
        adjEntry->id = msg->senderTable->sourceAddressLogical;
        adjEntry->quality = 0;
        adjEntry->pingBuffer = malloc(sizeof(bool)*SPT_MESSAGE_LQ_PING_BUFF_SIZE);
        adjEntry->pingBufferStart = 0;
        adjEntry->timestamp = get_epoch();
        spt->adjacencyTable->entries[spt->adjacencyTable->size] = adjEntry;
        spt->adjacencyTable->size++;
    }

    // We also need to record the ping to the ping buffer
    spt_ping_buffer_record(get_epoch(), true, adjEntry);

    // Now we'll update the quality
    adjEntry->quality = spt_ping_buffer_get_count(adjEntry);

    // 2. Adjacency & Reliability Test

    // And get quality from message (then use the lower of the two)
    adjacency_table_entry_t* adjacentMyself = NULL;
    // First locate
    for (i=0;i<msg->adjacencyTable->size;i++) {
        // NULL check probably the result of a bad design elsewhere, not a good solution
        if (msg->adjacencyTable->entries[i] != NULL && msg->adjacencyTable->entries[i]->id == spt->treeInfoTable->id) {
            adjacentMyself = msg->adjacencyTable->entries[i];
            break;
        }
    }

    // Then compare
    if (adjacentMyself != NULL && adjEntry->quality > adjacentMyself->quality) {
        adjEntry->quality = adjacentMyself->quality;
    }

    // Now check if we need to stop here (TEST)
    if (adjEntry->quality <= SPT_MESSAGE_LQ_RELIABILITY_THRESHOLD) { 
        ESP_LOGE(TAG, "Beacon Message failed reliability test");
        return; 
    }

    // 3. Core Table Test & Update Core Table
    // We're skipping this for now, I can't find core data

    // 4. Determine Ancestors
    bool updateAncestor = spt_beacon_should_be_ancestor(msg, spt);
    if (updateAncestor) {
        // Here we then know we need to update ancestor!
        // Neighborhood table props will be updated later :)
        spt->treeInfoTable->ancestorId = msg->senderTable->sourceAddressLogical;
    }   

    // 5. Update Tree & Neighborhood Tables
    if (updateAncestor) {
        spt->treeInfoTable->rootId = msg->rootAddressLogical;
        spt->treeInfoTable->cost = msg->cost + 1;
        spt->treeInfoTable->sequenceNumber = 4; // TODO: Support sequence number
        spt->treeInfoTable->pathMetric = spt_pathmetric_minimumcost(msg);

        // Now we remove descendant with neighborId == senderId
        spt_remove_neighbor(spt, msg->senderTable->sourceAddressLogical);

        // Then remove ancestor entry
        for (i=0;i<spt->neighborhoodTable->size;i++) {
            if (spt->neighborhoodTable->entries[i]->isAncestor) {
                spt_remove_neighbor(spt, spt->neighborhoodTable->entries[i]->neighborId);
                break;
            }
        }

        // And add new ancestor entry with message data
        pt_spt_neighborhood_entry_t* anc = malloc(sizeof(pt_spt_neighborhood_entry_t));
        anc->neighborId = msg->senderTable->sourceAddressLogical;
        anc->rootId = msg->rootAddressLogical;
        anc->isAncestor = true;
        anc->cost = msg->cost;
        anc->timestamp = msg->timestamp;
        anc->pathMetric = spt_pathmetric_minimumcost(msg);
        
        // Insert time
        spt_add_neighbor(spt, anc);

        // Done!
    } else if (msg->senderTable->sourceAddressLogical == spt->treeInfoTable->ancestorId) {
        if (msg->senderTable->sourceAddressLogical > spt->treeInfoTable->id) {
            spt->treeInfoTable->rootId = spt->treeInfoTable->id;
            spt->treeInfoTable->ancestorId = spt->treeInfoTable->id;
            spt->treeInfoTable->cost = 0;
            spt->treeInfoTable->pathMetric = spt_pathmetric_minimumcost(msg);

            // Then remove ancestor entry
            for (i=0;i<spt->neighborhoodTable->size;i++) {
                if (spt->neighborhoodTable->entries[i]->isAncestor) {
                    spt_remove_neighbor(spt, spt->neighborhoodTable->entries[i]->neighborId);
                    break;
                }
            }
            // Done!
        } else {
            spt->treeInfoTable->rootId = msg->rootAddressLogical;
            spt->treeInfoTable->ancestorId = msg->senderTable->sourceAddressLogical;
            spt->treeInfoTable->cost = msg->cost + 1;
            spt->treeInfoTable->sequenceNumber = 4; // TODO: Support sequence number
            spt->treeInfoTable->pathMetric = spt_pathmetric_minimumcost(msg);

            // Update ancestor entry with this message
            for (i=0;i<spt->neighborhoodTable->size;i++) {
                if (spt->neighborhoodTable->entries[i]->isAncestor) {
                    spt->neighborhoodTable->entries[i]->rootId = msg->rootAddressLogical;
                    spt->neighborhoodTable->entries[i]->cost = msg->cost + 1;
                    spt->neighborhoodTable->entries[i]->timestamp = msg->timestamp;
                    spt->neighborhoodTable->entries[i]->pathMetric = spt_pathmetric_minimumcost(msg);
                    break;
                }
            }
            // Done!
        }
    } else if (msg->parentAddressLogical == spt->treeInfoTable->id) {
        // We're the parent of the  sender, update neighbor table with descendant entry
        
        // First try to find the entry
        pt_spt_neighborhood_entry_t* desc = NULL;
        for (i=0;i<spt->neighborhoodTable->size;i++) {
            if (spt->neighborhoodTable->entries[i]->neighborId == msg->senderTable->sourceAddressLogical) {
                desc = spt->neighborhoodTable->entries[i];
                break;
            }
        }

        if (desc == NULL) {
            // Add
            desc = malloc(sizeof(pt_spt_neighborhood_entry_t));
            desc->neighborId = msg->senderTable->sourceAddressLogical;
            desc->rootId = msg->rootAddressLogical;
            desc->isAncestor = false;
            desc->cost = msg->cost;
            desc->timestamp = msg->timestamp;
            desc->pathMetric = spt_pathmetric_minimumcost(msg);

            spt_add_neighbor(spt, desc);
        } else {
            // Update
            desc->rootId = msg->rootAddressLogical;
            desc->cost = msg->cost;
            desc->timestamp = msg->timestamp;
            desc->pathMetric = spt_pathmetric_minimumcost(msg);
        }
        // Done!
    } else {
        // Remove descendant entry if exists
        spt_remove_neighbor(spt, msg->senderTable->sourceAddressLogical);
        // Done!
    }
}

void spt_handle_goodbye_message(spt_msg_goodbye_t* msg, hypercast_t* hypercast) {
    return;
}





// PROTOCOL SUPPORT FUNCTIONS
void spt_ping_buffer_record(uint64_t time, bool recordingPing, pt_spt_adjacency_entry_t* adjEntry) {
    // "Recording Ping" is a boolean which indicates whether we see a ping at this time
    // or we're just updating the buffer to check quality
    // We'll only update timestamp to match "time" if we're recording a ping

    long interval = (time - adjEntry->timestamp + SPT_MESSAGE_BEACON_TIME_INTERVAL/2) / SPT_MESSAGE_BEACON_TIME_INTERVAL;
    int i; // iterator

    // Then we check if the interval is 0
    if (interval < 1) {
        if (!recordingPing) { return; } // Interval 0 and not recording means time to leave!

        int end = adjEntry->pingBufferStart - 1;
        if (end < 0) { end = SPT_MESSAGE_LQ_PING_BUFF_SIZE - 1; }

        for (i=0;i<SPT_MESSAGE_LQ_PING_BUFF_SIZE;i++) {
            if (!adjEntry->pingBuffer[end]) {
                adjEntry->pingBuffer[end] = true;
                break;
            } else {
                end--;
                if  (end < 0) { end = SPT_MESSAGE_LQ_PING_BUFF_SIZE - 1; }
            }
        }

    } else {
        if (recordingPing) {
            for (i=0;i<interval;i++) {
                if (i < interval - 1) {
                    adjEntry->pingBuffer[adjEntry->pingBufferStart] = false;
                } else {
                    adjEntry->pingBuffer[adjEntry->pingBufferStart] = true;
                }
                // Then update start
                adjEntry->pingBufferStart = (adjEntry->pingBufferStart + 1) % SPT_MESSAGE_LQ_PING_BUFF_SIZE;
            }
        } else {
            int tempStart = adjEntry->pingBufferStart;
            for (i=0;i<interval-1;i++) {
                adjEntry->pingBuffer[tempStart] = false;
                tempStart = (tempStart + 1) % SPT_MESSAGE_LQ_PING_BUFF_SIZE;
            }
        }
    }

    // Finally update last ping received timestamp
    if (recordingPing) {
        adjEntry->timestamp = time;
    }
}

int spt_ping_buffer_get_count(pt_spt_adjacency_entry_t* adjEntry) {
    // Before counting, inject a ping
    spt_ping_buffer_record(get_epoch(), false, adjEntry);
    // Then count
    int i = adjEntry->pingBufferStart;
    int count_ = 0;
    for (int j=0;j<SPT_MESSAGE_LQ_PING_BUFF_SIZE;j++) {
        if (adjEntry->pingBuffer[i]) {
            count_++;
        }
        i = (i + 1) % SPT_MESSAGE_LQ_PING_BUFF_SIZE;
    }
    return count_;
}

bool spt_beacon_should_be_ancestor(spt_msg_beacon_t* msg, protocol_spt* spt) {
    if (msg->parentAddressLogical == spt->treeInfoTable->ancestorId) { return true; }

    // Get ancestor info from neighbor table
    pt_spt_neighborhood_entry_t* ancestor = NULL;
    for (int i=0;i<spt->neighborhoodTable->size;i++) {
        if (spt->neighborhoodTable->entries[i]->neighborId == spt->treeInfoTable->ancestorId) {
            ancestor = spt->neighborhoodTable->entries[i];
            break;
        }
    }

    if (msg->timestamp < spt->lastBeacon) { return false; }

    if (ancestor == NULL) {
        return spt_node_is_better_than(msg->rootAddressLogical, spt->treeInfoTable->id);
    }

    // By here we know ancestior != NULL
    if (spt_node_is_better_than(msg->rootAddressLogical, ancestor->rootId)) {
        return true;
    } else if (msg->rootAddressLogical == ancestor->rootId) {
        if (spt_pathmetric_minimumcost(msg) >= ancestor->pathMetric + SPT_JUMP_THRESHOLD 
            && msg->cost <= ancestor->cost + 2) { // 2 is hardcoded in Hypercast source
            return true;
        }
    }

    return false;
}

bool spt_node_is_better_than(uint32_t a, uint32_t b) {
    return a > b;
}

void spt_remove_neighbor(protocol_spt* spt, uint32_t neighborId) {
    // Remove neighbor from neighborhood table
    for (int i=0;i<spt->neighborhoodTable->size;i++) {
        if (spt->neighborhoodTable->entries[i]->neighborId == neighborId) {
            pt_spt_neighborhood_entry_t* entry = spt->neighborhoodTable->entries[i];
            // Move last entry to fill the gap
            spt->neighborhoodTable->entries[i] = spt->neighborhoodTable->entries[spt->neighborhoodTable->size - 1];
            spt->neighborhoodTable->size--;
            free(entry);
            break;
        }
    }
}

void spt_add_neighbor(protocol_spt* spt, pt_spt_neighborhood_entry_t* neighbor) {
    // First check if there's space in the table
    // Otherwise throw error and stop
    if (spt->neighborhoodTable->size >= SPT_TABLE_NEIGHBORHOOD_MAX_SIZE) {
        ESP_LOGE(TAG, "Neighborhood table is full: Neighbor add failed");
        return;
    }

    // Then add neighbor to neighborhood table
    spt->neighborhoodTable->entries[spt->neighborhoodTable->size] = neighbor;
    spt->neighborhoodTable->size++;
}


// Functions for freeing message memory
void spt_free_beacon_message(spt_msg_beacon_t* msg) {
    // Free the adjacency table
    for (int i=0;i<msg->adjacencyTable->size;i++) {
        free(msg->adjacencyTable->entries[i]);
    }
    free(msg->adjacencyTable->entries);
    free(msg->adjacencyTable);
    // Free sender table
    for (int i=0;i<msg->senderTable->size;i++) {
        free(msg->senderTable->entries[i]);
    }
    free(msg->senderTable->entries);
    free(msg->senderTable);
    // Now finish by freeing the message itself
    free(msg);
}

void spt_free_goodbye_message(spt_msg_goodbye_t* msg) {
    // Free sender table
    for (int i=0;i<msg->senderTable->size;i++) {
        free(msg->senderTable->entries[i]);
    }
    free(msg->senderTable);
    // Now finish by freeing the message itself
    free(msg);
}

int spt_pathmetric_minimumcost(spt_msg_beacon_t* beacon) {
    if (beacon == NULL) {
        return SPT_PATH_METRIC_FULL_VALUE;
    } else {
        return SPT_PATH_METRIC_FULL_VALUE - beacon->cost;
    }
}