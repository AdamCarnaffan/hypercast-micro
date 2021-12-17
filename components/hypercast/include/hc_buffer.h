#ifndef __HC_BUFFER_H__
#define __HC_BUFFER_H__

// First do our definitions
#define HC_BUFFER_DATA_MAX 1024

// Define the structs
typedef struct hc_packet {
    char *data;
    int size;
} hc_packet_t;

typedef struct hc_buffer {
    hc_packet_t **data;
    int current_size;
    int capacity;
    int front;
} hc_buffer_t;

// Now shape out the functions
void hc_allocate_buffer(hc_buffer_t *buffer, int length);
hc_packet_t* hc_pop_buffer(hc_buffer_t *buffer);
void hc_push_buffer(hc_buffer_t *buffer, char *data, int packet_length);

// Manage bytes IN
hc_packet_t* packet_snip_to_bytes(hc_packet_t*, int, int);
long long int packet_to_int(hc_packet_t*);

// Manage bytes OUT
int write_bytes(char*, long long d, int, int, int);

#endif

#ifndef TAG
#define TAG "HC_BUFFER"
#endif