#include <stdio.h>
#include "esp_log.h"

#include "hc_buffer.h"

void hc_allocate_buffer(hc_buffer_t *buffer, int length) {
    buffer->data = (hc_packet_t **)malloc(length * sizeof(hc_packet_t *));
    buffer->capacity = length;
    buffer->current_size = 0;
    buffer->front = 0;
}

hc_packet_t* hc_pop_buffer(hc_buffer_t *buffer) {
    // Before anything, check that the buffer isn't empty
    if (buffer->current_size == 0) {
        return NULL;
    }
    // ESP_LOGI(TAG, "data getting popped!");
    hc_packet_t *data = buffer->data[buffer->front];
    // Now that we've extracted the first entry, tidy the buffer
    buffer->data[buffer->front] = NULL;
    buffer->front = (buffer->front + 1) % buffer->capacity;
    buffer->current_size--;
    // Then return the data
    return data;
}

void hc_push_buffer(hc_buffer_t *buffer, char *data, int packet_length) {
    // First check if there is space
    if (buffer->current_size == buffer->capacity) {
        ESP_LOGE(TAG, "Buffer is full");
        return;
    }
    // Then allocate the data
    hc_packet_t *packet = (hc_packet_t *)malloc(sizeof(hc_packet_t));
    packet->data = data;
    packet->size = packet_length;
    // Add the data to the buffer
    buffer->data[(buffer->front + buffer->current_size) % buffer->capacity] = packet;
    buffer->current_size++;
}

char* packet_digest_to_bytes(hc_packet_t *packet, int lengthBits, int offsetBits) {
    /*
    * This function will take a packet and return a byte array of the digested portion
    * Note that the length is in BITS to account for those 4 bit datas
    */
    // Because packet->data has a hex string, 4 bits is the lowest tolerable length, and the offset must be a multiple of 4
    if (lengthBits < 4 || offsetBits % 4 != 0) {
        ESP_LOGE(TAG, "Invalid packet digest parameters");
        return NULL;
    }
    // According to the current input data we're using, only every 3rd and 4th byte is a valid hex character.
    // Each will account for 4 off the length, and only a third or fourth index will count for the offset
    // Before doing the parse, check that the packet size can accomadate the request
    if (packet->size < (lengthBits / 4) + (offsetBits / 4)) {
        ESP_LOGE(TAG, "Packet not large enough to digest");
        return NULL;
    }
    // Now we can start the digest
    char *digest = malloc(sizeof(char) * (lengthBits / 4 + 1));
    // Start with ye olde null termination
    digest[lengthBits / 4 + 1] = '\0';
    // Then digest the stuff that matters
    int dataPointer = 0; // This points in the packet data array at where we're lookin
    // Move the pointer by the offset
    if (offsetBits % 8 == 0) {
        dataPointer += (offsetBits / 2) - 1;
    } else {
        // Then it's a quirky 4 bit one
        dataPointer += (offsetBits - 4) / 2 + 2;
    }
    if (offsetBits == 0) { dataPointer = 2; } // Move to the first bit if the offset didn't do it
    for (int i = 0; i < lengthBits / 4; i++) {
        // First add to the digest array
        digest[i] = packet->data[dataPointer];
        // Then move the pointer for the next
        if (dataPointer % 4 == 3) {
            dataPointer += 3;
        } else {
            dataPointer += 1;
        }
    }
    return digest;
}