#ifndef __HC_ENGINE_H__
#define __HC_ENGINE_H__

#define SPT_BEACON_MESSAGE_TYPE 0
#define SPT_BEACON_MESSAGE_BASE_LENGTH 60
#define SPT_GOODBYE_MESSAGE_TYPE 1
#define SPT_GOODBYE_MESSAGE_BASE_LENGTH 0
#define SPT_ROUTE_REQ_MESSAGE_TYPE 2
#define SPT_ROUTE_REQ_MESSAGE_BASE_LENGTH 0
#define SPT_ROUTE_REPLY_MESSAGE_TYPE 3
#define SPT_ROUTE_REPLY_MESSAGE_BASE_LENGTH 0

typedef struct adjacency_table_entry {
    uint32_t id;
    uint8_t quality;
} adjacency_table_entry_t;

typedef struct adjacency_table {
    uint32_t size;
    adjacency_table_entry_t** entries;
} adjacency_table_t;

typedef struct spt_msg_beacon {
    uint32_t senderID;
    char* physicalAddress;
    uint32_t coreID;
    uint32_t ancestorID;
    uint16_t cost;
    uint16_t pathQuality;
    uint64_t sequenceNumber;
    adjacency_table_t* adjacencyTable;
} spt_msg_beacon_t;

void spt_parse(hc_packet_t*, int, long, long);

#endif

#ifndef TAG
#define TAG "HC_PROTOCOL_SPT"
#endif