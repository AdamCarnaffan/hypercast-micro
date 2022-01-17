#ifndef __HC_OVERLAY_H__
#define __HC_OVERLAY_H__

#include "hc_buffer.h"

#define HC_OVERLAY_MAX_EXTENSIONS 10
#define HC_OVERLAY_MAX_ROUTE_RECORD_LENGTH 256

// Overlay Extension Types
#define HC_OVERLAY_EXT_TYPE_NULL 0
#define HC_MSG_EXT_PAYLOAD_TYPE 2
#define HC_MSG_EXT_ROUTE_RECORD_TYPE 3

typedef struct hc_msg_overlay {
    uint8_t version;
    uint8_t dataMode;
    uint16_t hopLimit;
    uint32_t sourceLogicalAddress;
    uint32_t previousHopLogicalAddress;
    void **extensions; // Extensions are hashed into this list for easier retrieval
} hc_msg_overlay_t;

// Overlay Extensions
typedef struct hc_msg_ext {
    // Just a husk to let us read type and order before hashing in extensions array :)
    uint8_t type;
    uint8_t order;
} hc_msg_ext_t;

typedef struct hc_msg_ext_payload {
    // All extensions carry their type, and order of definition from the message
    uint8_t type;
    uint8_t order;
    // Then we have the extension data
    uint8_t length;
    char *payload;
} hc_msg_ext_payload_t;

typedef struct hc_msg_ext_route_record {
    // All extensions carry their type, and order of definition from the message
    uint8_t type;
    uint8_t order;
    // Then we have the extension data
    int routeRecordSize;
    uint32_t* routeRecordLogicalAddressList;
} hc_msg_ext_route_record_t;


hc_msg_overlay_t* hc_msg_overlay_parse(hc_packet_t*);
hc_packet_t* hc_msg_overlay_encode(hc_msg_overlay_t*);

// Helpers for managing hc_overlay
hc_msg_overlay_t* hc_msg_overlay_init();
void hc_msg_overlay_free(hc_msg_overlay_t*);
int hc_msg_overlay_insert_extension(hc_msg_overlay_t*, void*); // returns result (success = 1, failure = -1)
int hc_msg_overlay_get_primary_payload(hc_msg_overlay_t*, char**, int*); // returns result (success = 1, failure = -1)

#endif

#ifndef TAG
#define TAG "HC_OVERLAY"
#endif