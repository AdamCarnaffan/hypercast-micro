
#include "esp_log.h"
#include <string.h>

#include "hc_overlay.h"
#include "hc_protocols.h"

hc_msg_overlay_t* hc_msg_overlay_parse(hc_packet_t* packet) {
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
                ((hc_msg_ext_route_record_t*)ext)->routeRecord = 0; // TODO: Implement
                ((hc_msg_ext_route_record_t*)ext)->routeRecordLogicalAddress = packet_to_int(packet_snip_to_bytes(packet, 32, extensionStartIndex + 24));
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
                write_bytes(data, 4, 8, extensionStartIndex + 16, HC_BUFFER_DATA_MAX); // extension length
                write_bytes(data, ((hc_msg_ext_route_record_t*)ext)->routeRecordLogicalAddress, 32, extensionStartIndex + 24, HC_BUFFER_DATA_MAX); 
                // TODO: Impelment route record
                thisExtensionLength += 4;
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

    ESP_LOGI(TAG, "Extensions found: %d", extensionsFound);

    // Now dataSize is extensionStartIndex but in bytes not bits
    dataSize = extensionStartIndex / 8;

    // Then we finish by throwing the first extension type to the beginning
    if (extensionsFound > 0) {
        write_bytes(data, ((hc_msg_ext_t*)extensionsOrdered[0])->type, 8, 72, HC_BUFFER_DATA_MAX);
    } else {
        write_bytes(data, 0, 8, 72, HC_BUFFER_DATA_MAX);
    }
    // Now we're done with the ordered extensions array, free it
    // free(extensionsOrdered);

    // And we add the length of the extensions put together
    write_bytes(data, extensionsLength, 16, 40, HC_BUFFER_DATA_MAX);
    // Now at the end let's pretty it up!
    hc_packet_t *packet = malloc(sizeof(hc_packet_t));
    packet->size = dataSize;
    packet->data = malloc(sizeof(char)*dataSize);
    memcpy(packet->data, data, dataSize);
    // Then we can free the original data (because its been copied)
    // free(data);
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
    // First free allocated extensions
    for (int i=0;i<HC_OVERLAY_MAX_EXTENSIONS;i++) {
        if (msg->extensions[i] != NULL) {
            // First check (based on type) if we need to free sub-components
            switch (((hc_msg_ext_t*)msg->extensions[i])->type) {
                case HC_MSG_EXT_PAYLOAD_TYPE:
                    free(((hc_msg_ext_payload_t*)msg->extensions[i])->payload);
                    break;
                default:
                    // Some extensions will have nothing to free
                    break;
            }
            free(msg->extensions[i]);
        }
    }
    // Then free the extensions array
    free(msg->extensions);
    // Then free the message
    free(msg);
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