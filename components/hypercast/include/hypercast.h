#include "hc_buffer.h"

#ifndef __HC_MAIN_H__
#define __HC_MAIN_H__

#define HC_BUFFER_SIZE 100

typedef struct hc_config {
    int number; // This is a placeholder
} hc_config_t;

// Define our state machine
typedef struct hypercast {
    // Add 2 buffers for send and receive
    hc_buffer_t *receiveBuffer;
    hc_buffer_t *sendBuffer;
    // Add the socket
    int socket; // Really just a file pointer, but a *special* file pointer
    // Introduce statefulness
    int state;
    // Then cofiguration
    void *protocol; // protocol is actually an allocated object of a type based on config (always castable to protocol shell)
    hc_config_t config;
} hypercast_t;

typedef struct hc_protocol_shell {
    int id;
    // actual protocols have other data that follows
} hc_protocol_shell_t;

// Now just init here
void hc_init(void*);
void hc_install_config(hypercast_t*);

#endif

#ifndef TAG
#define TAG "HC_MAIN"
#endif