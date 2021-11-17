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
