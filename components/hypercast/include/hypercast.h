#include "hc_buffer.h"

#ifndef __HC_MAIN_H__
#define __HC_MAIN_H__

#define HC_BUFFER_SIZE 100

typedef struct hc_config {
    int number; // This is a placeholder
} hc_config_t;

typedef struct hc_ipv4_addr {
    uint8_t addr[4];
} hc_ipv4_addr_t;

typedef struct hc_sender_entry {
    uint16_t type;
    uint16_t hash;
    uint8_t addressLength; // This may include both address and port length?
    hc_ipv4_addr_t* address; // address_length - 2 bytes to port (IPV4 specific probably)
    uint16_t port;
} hc_sender_entry_t;

typedef struct hc_sender_table {
    int size;
    hc_sender_entry_t **entries;
    int sourceAddressLogical;
} hc_sender_table_t;

// Define our state machine
typedef struct hypercast {
    // Add 2 buffers for send and receive
    hc_buffer_t *receiveBuffer;
    hc_buffer_t *sendBuffer;
    // Add the Conection Info
    int socket; // Really just a file pointer, but a *special* file pointer
    hc_sender_table_t *senderTable;
    // Introduce statefulness
    int state;
    // Then cofiguration
    void *protocol; // protocol is actually an allocated object of a type based on config (always castable to protocol shell)
    hc_config_t config;
    // Also install the callback!
    void (*callback)(char *, int); // data, length
} hypercast_t;

typedef struct hc_protocol_shell {
    int id;
    // actual protocols have other data that follows
} hc_protocol_shell_t;

// Now just init here
void hc_init(void*);
void hc_install_config(hypercast_t*);

// callback
void hc_callback_handler(char*, int);

#endif

#ifndef TAG
#define TAG "HC_MAIN"
#endif