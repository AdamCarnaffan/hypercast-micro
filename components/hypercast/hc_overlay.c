
#include <string.h>

#include "hc_overlay.h"
#include "hc_protocols.h"

static const char* TAG = "HC_OVERLAY";

hc_msg_overlay_t* hc_msg_overlay_parse(hc_packet_t* packet) {
    // Before beginning to parse, check that the packet meets minimum length requirement
    if (packet->size < HC_MSG_OVERLAY_MIN_LENGTH/8) {
        ESP_LOGE(TAG, "Packet too small to be an overlay message");
        return NULL;
    }

    // Start by initializing the overlay message
    hc_msg_overlay_t* msg = hc_msg_overlay_init();

    // Let's do the parse now
    msg->version = packet_to_int(packet_snip_to_bytes(packet, 4, 8));
    msg->dataMode = packet_to_int(packet_snip_to_bytes(packet, 4, 12));
    msg->hopLimit = packet_to_int(packet_snip_to_bytes(packet, 16, 56));
    msg->sourceLogicalAddress = packet_to_int(packet_snip_to_bytes(packet, 32, 88));
    msg->previousHopLogicalAddress = packet_to_int(packet_snip_to_bytes(packet, 32, 120));

    // Then finish with parses of extensions
    int extensionStartIndex = 152;
    int extensionType = packet_to_int(packet_snip_to_bytes(packet, 8, 72));
    int extensionOrder = 1;
    int extensionLength = 0;
    int extendResult = 1;
    void* ext;

    while (extensionType != 0 && extendResult > 0) {
        // Let's build the extension
        switch (extensionType) {
            case HC_MSG_EXT_PAYLOAD_TYPE:
                // This type just includes the standard, plus a string payload
                ext = malloc(sizeof(hc_msg_ext_payload_t));
                ((hc_msg_ext_payload_t*)ext)->type = extensionType;
                ((hc_msg_ext_payload_t*)ext)->order = extensionOrder;
                // Now we sort out the payload
                ((hc_msg_ext_payload_t*)ext)->length = packet_to_int(packet_snip_to_bytes(packet, 8, extensionStartIndex + 16));
                ((hc_msg_ext_payload_t*)ext)->payload = malloc(sizeof(char) * ((hc_msg_ext_payload_t*)ext)->length);
                // We've done prep, time to extract and copy the payload over
                hc_packet_t* payloadPacket = packet_snip_to_bytes(packet, 8*((hc_msg_ext_payload_t*)ext)->length, extensionStartIndex + 24);
                if (payloadPacket == NULL) {
                    ESP_LOGE(TAG, "Failed to extract payload from extension");
                    return NULL;
                }
                memcpy(((hc_msg_ext_payload_t*)ext)->payload, payloadPacket->data, payloadPacket->size);
                // Then cleanup
                free_packet(payloadPacket);
                // Now we need to track extensionLength so the next one starts in the right place
                extensionLength = ((hc_msg_ext_payload_t*)ext)->length*8 + 8; // +8 for the length of the payload
                break;
            case HC_MSG_EXT_ROUTE_RECORD_TYPE:
                // This type includes the standard plus a route record and logical address
                ext = malloc(sizeof(hc_msg_ext_route_record_t));
                ((hc_msg_ext_route_record_t*)ext)->type = extensionType;
                ((hc_msg_ext_route_record_t*)ext)->order = extensionOrder;
                // Now get the size of the route record and each entry
                // Note that the size of the route record is /4 because each entry is 4 bytes
                ((hc_msg_ext_route_record_t*)ext)->routeRecordSize = packet_to_int(packet_snip_to_bytes(packet, 8, extensionStartIndex + 16)) / 4;
                // We'll also allocate a routeRecordAddressList of MAX size
                ((hc_msg_ext_route_record_t*)ext)->routeRecordLogicalAddressList = malloc(sizeof(uint32_t) * HC_OVERLAY_MAX_ROUTE_RECORD_LENGTH);
                // Now iterate from extensionStartIndex + 24 to get each entry (32 long)
                for (int i=0; i<((hc_msg_ext_route_record_t*)ext)->routeRecordSize; i++) {
                    ((hc_msg_ext_route_record_t*)ext)->routeRecordLogicalAddressList[i] = packet_to_int(packet_snip_to_bytes(packet, 32, extensionStartIndex + 24 + (i*32)));
                }
                break;
            default:
                ESP_LOGE(TAG, "Unknown extension type: %d", extensionType);
                extendResult = -1;
                break;
        }

        // Check if extension interpretation failed
        if (extendResult < 0) { break; }
        

        // Then we'll extend the msg
        extendResult = hc_msg_overlay_insert_extension(msg, ext);

        // And finish by getting the next extension type!
        extensionType = packet_to_int(packet_snip_to_bytes(packet, 8, extensionStartIndex));
        // Then increment iterators :)
        extensionOrder++;
        extensionStartIndex += 16 + extensionLength;
    }

    return msg;
}

hc_packet_t* hc_msg_overlay_encode(hc_msg_overlay_t* msg) {
    // Start by initializing a place to build the packet
    char data[HC_BUFFER_DATA_MAX]; // Temporary buffer of max size to shove data into
    int dataSize = 0;
    // Then let's start by encoding the version
    write_bytes(data, HC_PROTOCOL_OVERLAY_MESSAGE, 4, 0, HC_BUFFER_DATA_MAX);
    write_bytes(data, 0, 4, 4, HC_BUFFER_DATA_MAX);
    write_bytes(data, msg->version, 4, 8, HC_BUFFER_DATA_MAX);
    write_bytes(data, msg->dataMode, 4, 12, HC_BUFFER_DATA_MAX);
    // Now we insert 0 from 16 to 40 (3 bytes). No idea why tho
    write_bytes(data, 0, 24, 16, HC_BUFFER_DATA_MAX);
    // Here we leave a space from 40 to 56 for a count of the extensions' bytes put together
    write_bytes(data, msg->hopLimit, 16, 56, HC_BUFFER_DATA_MAX);
    // Then we leave a space from 72 to 80 for the first extension's type
    write_bytes(data, 4, 8, 80, HC_BUFFER_DATA_MAX); // This is the length of logical addresses in bytes (hardcoded to 4)
    write_bytes(data, msg->sourceLogicalAddress, 32, 88, HC_BUFFER_DATA_MAX);
    ESP_LOGI(TAG, "Source previous hop address: %d", msg->previousHopLogicalAddress);
    write_bytes(data, msg->previousHopLogicalAddress, 32, 120, HC_BUFFER_DATA_MAX);
    
    int extensionStartIndex = 152;

    // Now we start writing the extensions out
    // First we're doing extension discovery
    int extensionsFound = 0;
    int i;
    bool extensionFoundOnIter;
    void** extensionsOrdered = malloc(sizeof(void*)*HC_OVERLAY_MAX_EXTENSIONS);
    // Before we load extensions in, set null on all
    for (i = 0; i < HC_OVERLAY_MAX_EXTENSIONS; i++) {
        extensionsOrdered[i] = NULL;
    }

    while (1) { // Just iterate until the break condition
        // Find extension in msg->extensions with order = extensionsFound + 1
        extensionFoundOnIter = false;
        for (i=0;i<HC_OVERLAY_MAX_EXTENSIONS;i++) {
            if (msg->extensions[i] != NULL && ((hc_msg_ext_t*)msg->extensions[i])->order == extensionsFound + 1) {
                extensionsOrdered[extensionsFound] = msg->extensions[i];
                extensionsFound++;
                extensionFoundOnIter = true;
                break;
            }
        }
        if (!extensionFoundOnIter) { break; }
    }

    // Track data size before extensions
    dataSize = extensionStartIndex;

    int extensionsLength = 0;
    int thisExtensionLength;
    int nextExtensionType;

    // Now we have the extensions in order! Let's encode them
    hc_msg_ext_t* ext;
    hc_msg_ext_t* next;
    for (i=0;i<extensionsFound;i++) {
        // Before encoding, setup data for the encode
        ext = (hc_msg_ext_t*)extensionsOrdered[i];
        next = (hc_msg_ext_t*)extensionsOrdered[i+1];
        if (next == NULL) {
            nextExtensionType = 0;
        } else {
            nextExtensionType = next->type;
        }
        thisExtensionLength = 0;
        // First we'll encode the extension standards
        // We encode the NEXT extension's type (or 0 if there is no next extension)
        write_bytes(data, nextExtensionType, 8, extensionStartIndex, HC_BUFFER_DATA_MAX);
        // Then it's the extension length size (which is 1)
        write_bytes(data, 1, 8, extensionStartIndex + 8, HC_BUFFER_DATA_MAX);
        // Then the extension length itself (set determined by case)
        thisExtensionLength = 3;
        // Then these are specific to the extension type
        switch (ext->type) {
            case HC_MSG_EXT_PAYLOAD_TYPE:
                write_bytes(data, ((hc_msg_ext_payload_t*)ext)->length, 8, extensionStartIndex + 16, HC_BUFFER_DATA_MAX); // extension length
                write_chars_to_bytes(data, ((hc_msg_ext_payload_t*)ext)->payload, ((hc_msg_ext_payload_t*)ext)->length*8, 
                                                        extensionStartIndex + 24, HC_BUFFER_DATA_MAX);
                thisExtensionLength += ((hc_msg_ext_payload_t*)ext)->length;
                break;
            case HC_MSG_EXT_ROUTE_RECORD_TYPE:
                // Size is really easy, it's 4*the size of the route record (4 bytes per address)
                write_bytes(data, ((hc_msg_ext_route_record_t*)ext)->routeRecordSize*4, 8, extensionStartIndex + 16, HC_BUFFER_DATA_MAX);
                // Now we'll encode the route record logical addresses iteratively
                for (int j=0;j<((hc_msg_ext_route_record_t*)ext)->routeRecordSize;j++) {
                    write_bytes(data, ((hc_msg_ext_route_record_t*)ext)->routeRecordLogicalAddressList[j], 32, extensionStartIndex + 24 + j*32, HC_BUFFER_DATA_MAX);
                }
                thisExtensionLength += ((hc_msg_ext_route_record_t*)ext)->routeRecordSize*4;
                break;
            default:
                ESP_LOGE(TAG, "Unknown extension type: %d", ext->type);
                break;
        }
        // Then we'll update the extensionsLength
        extensionsLength += thisExtensionLength;
        // and update extensionStartIndex
        extensionStartIndex += thisExtensionLength*8;
    }

    // ESP_LOGI(TAG, "Extensions found: %d", extensionsFound);

    // Now dataSize is extensionStartIndex but in bytes not bits
    dataSize = extensionStartIndex / 8;

    // Then we finish by throwing the first extension type to the beginning
    if (extensionsFound > 0) {
        write_bytes(data, ((hc_msg_ext_t*)extensionsOrdered[0])->type, 8, 72, HC_BUFFER_DATA_MAX);
    } else {
        write_bytes(data, 0, 8, 72, HC_BUFFER_DATA_MAX);
    }
    // Now we're done with the ordered extensions array, free it
    free(extensionsOrdered);

    // And we add the length of the extensions put together
    write_bytes(data, extensionsLength, 16, 40, HC_BUFFER_DATA_MAX);
    // Now at the end let's pretty it up!
    hc_packet_t *packet = malloc(sizeof(hc_packet_t));
    packet->size = dataSize;
    packet->data = malloc(sizeof(char)*dataSize);
    memcpy(packet->data, data, dataSize);
    return packet;
}

hc_msg_overlay_t* hc_msg_overlay_init() {
    hc_msg_overlay_t* msg = malloc(sizeof(hc_msg_overlay_t));

    // Now init the extensions array
    msg->extensions = malloc(sizeof(void*)*HC_OVERLAY_MAX_EXTENSIONS);
    for (int i=0;i<HC_OVERLAY_MAX_EXTENSIONS;i++) {
        msg->extensions[i] = NULL;
    }
    // Then return the initialized message
    return msg;
}

void hc_msg_overlay_free(hc_msg_overlay_t* msg) {
    hc_msg_overlay_free_extensions(msg->extensions);
    // Then free the message
    free(msg);
}

void hc_msg_overlay_free_extensions(void** extensions) {
    // First free allocated extensions
    for (int i=0;i<HC_OVERLAY_MAX_EXTENSIONS;i++) {
        if (extensions[i] != NULL) {
            // First check (based on type) if we need to free sub-components
            switch (((hc_msg_ext_t*)extensions[i])->type) {
                case HC_MSG_EXT_PAYLOAD_TYPE:
                    free(((hc_msg_ext_payload_t*)extensions[i])->payload);
                    break;
                case HC_MSG_EXT_ROUTE_RECORD_TYPE:
                    free(((hc_msg_ext_route_record_t*)extensions[i])->routeRecordLogicalAddressList);
                    break;
                default:
                    // Some extensions will have nothing to free
                    break;
            }
            free(extensions[i]);
        }
    }
    // Then free the extensions array
    free(extensions);
}

hc_msg_overlay_t* hc_msg_overlay_init_with_payload(hypercast_t* hypercast, char* payload, int payloadLength) {
    hc_msg_overlay_t* msg = hc_msg_overlay_init();
    // Now populate body of message
    msg->version = 3;
    msg->dataMode = 1;
    msg->hopLimit = 254;
    msg->sourceLogicalAddress = hypercast->senderTable->sourceAddressLogical;
    msg->previousHopLogicalAddress = hypercast->senderTable->sourceAddressLogical;
    // Then add payload extension
    hc_msg_ext_payload_t* ext = malloc(sizeof(hc_msg_ext_payload_t));
    ext->type = HC_MSG_EXT_PAYLOAD_TYPE;
    ext->order = 1;
    ext->length = payloadLength;
    ext->payload = malloc(sizeof(char)*payloadLength);
    memcpy(ext->payload, payload, payloadLength);
    hc_msg_overlay_insert_extension(msg, ext);
    // Now add the route record extension
    hc_msg_ext_route_record_t* ext2 = malloc(sizeof(hc_msg_ext_route_record_t));
    ext2->type = HC_MSG_EXT_ROUTE_RECORD_TYPE;
    ext2->order = 2;
    ext2->routeRecordSize = 1;
    ext2->routeRecordLogicalAddressList = malloc(sizeof(uint32_t)*ext2->routeRecordSize);
    ext2->routeRecordLogicalAddressList[0] = hypercast->senderTable->sourceAddressLogical;
    hc_msg_overlay_insert_extension(msg, ext2);
    return msg;
}

int hc_msg_overlay_insert_extension(hc_msg_overlay_t* msg, void* extension) {
    // Because the limit for extensions is so low, we can just do a dumb hash here
    int type = ((hc_msg_ext_t*)extension)->type;
    int insertIndex = type;
    
    // Try insert at "type" position then continue trying if we fail
    while (msg->extensions[insertIndex] != NULL) {
        insertIndex++;
        if (insertIndex >= HC_OVERLAY_MAX_EXTENSIONS) {
            insertIndex = 0;
        }
        if (insertIndex == type) {
            // We've tried all the positions, and we're still failing
            ESP_LOGE(TAG, "Failed to insert extension, no extension slots available in Overlay Message");
            return -1;
        }
    }
    
    // Now we know that "insertIndex" is a free slot, so we can insert
    msg->extensions[insertIndex] = extension;

    // Then we're done
    return 1;
}

int hc_msg_overlay_get_primary_payload(hc_msg_overlay_t* msg, char** payload_destination, int* payload_length) {
    // Find payload extensions in the extension array
    int payloadExtensionIndex = HC_MSG_EXT_PAYLOAD_TYPE;
    
    void* extension;

    while (msg->extensions[payloadExtensionIndex] != NULL) {
        // We're basically looking for the first extension of the correct type
        // (because of how ordering works)
        if (((hc_msg_ext_t*)msg->extensions[payloadExtensionIndex])->type == HC_MSG_EXT_PAYLOAD_TYPE) {
            // We found the first payload extension, so let's return it
            extension = msg->extensions[payloadExtensionIndex];
            *payload_destination = ((hc_msg_ext_payload_t*)extension)->payload;
            *payload_length = ((hc_msg_ext_payload_t*)extension)->length;
            return 1;
        }
        payloadExtensionIndex++;
        if (payloadExtensionIndex >= HC_OVERLAY_MAX_EXTENSIONS) {
            payloadExtensionIndex = 0;
        }
        if (payloadExtensionIndex == HC_MSG_EXT_PAYLOAD_TYPE) {
            // We've tried all the positions, and we're still failing
            ESP_LOGE(TAG, "Failed to find payload extension in Overlay Message when trying to retrieve payload");
            return -1;
        }
    }

    return -1;
}

int hc_msg_overlay_retrieve_extension_of_type(hc_msg_overlay_t* msg, int type, void** extension) {
     // Find payload extensions in the extension array
    int extensionIndex = type;

    while (msg->extensions[extensionIndex] != NULL) {
        // We're basically looking for the first extension of the correct type
        // (because of how ordering works)
        if (((hc_msg_ext_t*)msg->extensions[extensionIndex])->type == type) {
            // We found the first payload extension, so let's return it
            *extension = msg->extensions[extensionIndex];
            return 1;
        }
        extensionIndex++;
        if (extensionIndex >= HC_OVERLAY_MAX_EXTENSIONS) {
            extensionIndex = 0;
        }
        if (extensionIndex == type) {
            // We've tried all the positions, and we're still failing
            ESP_LOGE(TAG, "Failed to find retrieve extension of type %d in Overlay Message", type);
            return -1;
        }
    }

    return -1;
}

int hc_msg_overlay_ext_get_next_order(hc_msg_overlay_t* msg) {
    // Find the next order in the extension array
    int extensionIndex = 0;
    int maxOrder = 0;
    hc_msg_ext_t* extension;
    for (extensionIndex=0;extensionIndex<HC_OVERLAY_MAX_EXTENSIONS;extensionIndex++) {
        if (msg->extensions[extensionIndex] == NULL) { continue; }
        // Now we want to find the order number of this extension
        extension = (hc_msg_ext_t*)msg->extensions[extensionIndex];
        if (extension->order > maxOrder) {
            maxOrder = extension->order;
        }
    }

    return maxOrder + 1;
}

int hc_overlay_route_record_contains(hc_msg_overlay_t* msg, int logicalAddress) {
    // First check if there is a route record
    hc_msg_ext_route_record_t* routeRecord = NULL;
    int recordFindResult = hc_msg_overlay_retrieve_extension_of_type(msg, HC_MSG_EXT_ROUTE_RECORD_TYPE, (void*)&routeRecord);

    if (recordFindResult == 1) {
        // We found a route record, so let's check if it contains the logical address
        for (int i=0;i<routeRecord->routeRecordSize;i++) {
            if (routeRecord->routeRecordLogicalAddressList[i] == logicalAddress) {
                return 1;
            }
        }
    }

    return 0;
}

void hc_overlay_route_record_append(hc_msg_overlay_t* msg, int logicalAddress) {
    // First check if there is a route record
    hc_msg_ext_route_record_t* routeRecord = NULL;
    int recordFindResult = hc_msg_overlay_retrieve_extension_of_type(msg, HC_MSG_EXT_ROUTE_RECORD_TYPE, (void*)&routeRecord);

    // If not, add one
    if (recordFindResult == -1) {
        routeRecord = malloc(sizeof(hc_msg_ext_route_record_t));
        routeRecord->type = HC_MSG_EXT_ROUTE_RECORD_TYPE;
        routeRecord->order = hc_msg_overlay_ext_get_next_order(msg);
        routeRecord->routeRecordSize = 1;
        routeRecord->routeRecordLogicalAddressList = malloc(sizeof(uint32_t) * HC_OVERLAY_MAX_ROUTE_RECORD_LENGTH);
        // Add the message sender to the table automatically
        routeRecord->routeRecordLogicalAddressList[0] = msg->sourceLogicalAddress;
        hc_msg_overlay_insert_extension(msg, (void*)routeRecord);
    }

    // Now we'll add our logical address to the route record
    routeRecord->routeRecordLogicalAddressList[routeRecord->routeRecordSize] = logicalAddress;
    routeRecord->routeRecordSize++;

    // Done!
}