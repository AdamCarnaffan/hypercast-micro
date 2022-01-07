#include <stdio.h>
#include <math.h>
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

hc_packet_t* packet_snip_to_bytes(hc_packet_t *packet, int lengthBits, int offsetBits) {
    /*
    * This function will take a packet and return a pakcet of JUST the digested portion
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
    if (packet->size < (lengthBits / 8) + (offsetBits / 8)) {
        ESP_LOGE(TAG, "Packet not large enough to digest");
        return NULL;
    }
    // Now we can start the digest
    char *digest = malloc(sizeof(char) * (ceil((double)lengthBits / 8)));
    // We'll do a bit shift (kind of magic)
    // Shift the packet->data by the offset bits, then mask with num bits in length as 1, others 0
    // digest = ((char *)packet->data >> offsetBits) & ((1 << lengthBits) - 1);
    int remainingBits = lengthBits;
    int currentBit = offsetBits;
    char byteTarget = 0x00;
    while (remainingBits > 0) {
        // First get the bit from the packet that we want
        byteTarget = packet->data[currentBit / 8];
        if (currentBit % 8 == 0) { byteTarget = byteTarget >> 4; }
        else { byteTarget = byteTarget & 0x0F; }
        // Now look to set the target half
        if ((lengthBits - remainingBits) % 8 == 0) { 
            digest[(lengthBits - remainingBits) / 8] = byteTarget; 
        } else {
            digest[(lengthBits - remainingBits) / 8] = (digest[(lengthBits - remainingBits) / 8] << 4) | byteTarget;
        }
        // Now tick
        remainingBits -= 4;
        currentBit += 4;
    }

    // Finish with ye olde packet building
    hc_packet_t *snipped_packet = (hc_packet_t *)malloc(sizeof(hc_packet_t));
    snipped_packet->data = digest;
    snipped_packet->size = ceil((double)lengthBits / 8);
    return snipped_packet;
}

long long int packet_to_int(hc_packet_t* packet) {
    // Take null terminated char* and convert to long long int
    long long int result = 0;
    for (int i = 0; i < packet->size; i++) {
        result = result << 8;
        result += packet->data[i];
    }
    return result;
}

int write_bytes(char* dataString, long long writeData, int lengthBits, int offsetBits, int dataArraySize) {
    // First check that we're writing to a reasonable place in the byte string
    if (lengthBits % 4 != 0 || offsetBits % 4 != 0) {
        ESP_LOGE(TAG, "Invalid data write parameters");
        return -1;
    }
    // Now we'll also check that we're in bounds of the byte string
    if (dataArraySize < (lengthBits / 8) + (offsetBits / 8)) {
        ESP_LOGE(TAG, "Data write array not large enough to write to");
        return -1;
    }
    // Then it's time to begin!
    int bitsToWrite = lengthBits;
    int currentBit = offsetBits;
    while (bitsToWrite > 0) {
        // We'll always index dataString with
        // dataString[currentBit / 8]
        // And the data to write will always be shifted by the bits to write
        // writeData = writeData >> bitsToWrite;
        // Check if we're targeting the second half
        if (currentBit % 8 == 0) {
            // Writing first half
            dataString[currentBit / 8] = (dataString[currentBit / 8] & 0x0F) | (((writeData >> (bitsToWrite - 4)) & 0xF) << 4);
        } else {
            // Writing second half
            dataString[currentBit / 8] = (dataString[currentBit / 8] & 0xF0) | ((writeData >> (bitsToWrite - 4)) & 0xF);
        }
        // Now tick
        bitsToWrite -= 4;
        currentBit += 4;
    }


    return (currentBit/8) + 1; // Returns the 1-index in the data array of the last byte written
}

int write_chars_to_bytes(char* dataString, char* writeData, int lengthBits, int offsetBits, int dataArraySize) {
    // First check that we're writing to a reasonable place in the byte string
    if (lengthBits % 8 != 0 || offsetBits % 8 != 0) {
        ESP_LOGE(TAG, "Invalid data write parameters");
        return -1;
    }
    // Now we'll also check that we're in bounds of the byte string
    if (dataArraySize < (lengthBits / 8) + (offsetBits / 8)) {
        ESP_LOGE(TAG, "Data write array not large enough to write to");
        return -1;
    }
    // Then it's time to begin!
    int bitsToWrite = lengthBits;
    int currentBit = offsetBits;
    while (bitsToWrite > 0) {
        // Because we're writing charts here, we can simply use bytes,
        // and each char is 1 byte in, 1 byte out, plain and simple :)
        dataString[currentBit / 8] = writeData[bitsToWrite - lengthBits];
        // Now tick
        bitsToWrite -= 8;
        currentBit += 8;
    }


    return (currentBit/8) + 1; // Returns the 1-index in the data array of the last byte written
}

void free_packet(hc_packet_t* packet) {
    free(packet->data);
    free(packet);
}