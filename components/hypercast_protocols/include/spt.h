#ifndef __HC_ENGINE_H__
#define __HC_ENGINE_H__

#include "hypercast.h"

#define SPT_BEACON_MESSAGE_TYPE 0
#define SPT_BEACON_MESSAGE_BASE_LENGTH 60
#define SPT_GOODBYE_MESSAGE_TYPE 1
#define SPT_GOODBYE_MESSAGE_BASE_LENGTH 0
#define SPT_ROUTE_REQ_MESSAGE_TYPE 2
#define SPT_ROUTE_REQ_MESSAGE_BASE_LENGTH 0
#define SPT_ROUTE_REPLY_MESSAGE_TYPE 3
#define SPT_ROUTE_REPLY_MESSAGE_BASE_LENGTH 0

typedef struct sender_table_entry {
    uint16_t type;
    uint16_t hash;
    uint8_t addressLength; // This may include both address and port length?
    char* address; // address_length - 2 bytes to port (IPV4 specific probably)
    uint16_t port;
} sender_table_entry_t;

typedef struct sender_table {
    int interfaceCount;
    sender_table_entry_t** entries;
    int sourceAddressLogical;
} sender_table_t;

typedef struct adjacency_table_entry {
    uint32_t id;
    uint8_t quality;
} adjacency_table_entry_t;

typedef struct adjacency_table {
    uint32_t size;
    adjacency_table_entry_t** entries;
} adjacency_table_t;

typedef struct spt_msg_beacon {
    sender_table_t* senderTable;
    uint32_t rootAddressLogical;
    uint32_t parentAddressLogical;
    uint32_t cost;
    uint64_t timestamp;
    uint16_t senderCount;
    adjacency_table_t* adjacencyTable;
    uint16_t reliability;
} spt_msg_beacon_t;

typedef struct protocol_spt {
    int id;
    int dog;
    // Tree info table
    // neighborhood table
    // backup ancestor table
    // adjacency table
    // core table
} protocol_spt;

void spt_parse(hc_packet_t*, int, long, long, hypercast_t*);
hc_packet_t* spt_package(void* msg, int, int, hypercast_t*);
void spt_maintenance(hypercast_t*);

protocol_spt* spt_protocol_from_config();

#endif

#ifndef TAG
#define TAG "HC_PROTOCOL_SPT"
#endif