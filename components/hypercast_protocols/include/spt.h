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

typedef struct adjacency_table_entry {
    uint32_t id;
    uint8_t quality;
} adjacency_table_entry_t;

typedef struct adjacency_table {
    uint32_t size;
    adjacency_table_entry_t** entries;
} adjacency_table_t;

typedef struct spt_msg_beacon {
    hc_sender_table_t* senderTable;
    uint32_t rootAddressLogical;
    uint32_t parentAddressLogical;
    uint32_t cost;
    uint64_t timestamp;
    uint16_t senderCount;
    adjacency_table_t* adjacencyTable;
    uint16_t reliability;
} spt_msg_beacon_t;

typedef struct spt_msg_goodbye {
    hc_sender_table_t* senderTable;
} spt_msg_goodbye_t;


typedef struct pt_spt_tree_info_table {
    uint16_t id;
    uint32_t physicalAddress;
    uint16_t coreId;
    uint32_t ancestorId;
    uint32_t cost;
    uint32_t pathMetric; // This should probably be an enum?
    uint32_t sequenceNumber;
} pt_spt_tree_info_table_t;

typedef struct pt_spt_neighborhood_entry {
    uint16_t neighborId;
    uint32_t physicalAddress;
    uint16_t coreId;
    uint32_t cost;
    uint32_t pathMetric;
    uint64_t timestamp;
    bool isAncestor;
} pt_spt_neighborhood_entry_t;

typedef struct pt_spt_neighborhood_table {
    int size;
    pt_spt_neighborhood_entry_t** entries;
} pt_spt_neighborhood_table_t;

typedef struct pt_spt_backup_ancestor_entry {
    uint16_t neighborId;
    uint32_t physicalAddress;
    uint16_t coreId;
    uint32_t cost;
    uint32_t pathMetric;
    uint64_t timestamp;
} pt_spt_backup_ancestor_entry_t;

typedef struct pt_spt_backup_ancestor_table {
    int size;
    int maxSize;
    pt_spt_backup_ancestor_entry_t** entries;
} pt_spt_backup_ancestor_table_t;

typedef struct pt_spt_adjacency_entry {
    int id;
    uint32_t quality;
    uint64_t timestamp;
} pt_spt_adjacency_entry_t;

typedef struct pt_spt_adjacency_table {
    int size;
    pt_spt_adjacency_entry_t** entries;
} pt_spt_adjacency_table_t;

typedef struct pt_spt_core_entry {
    uint16_t id;
    uint16_t sequenceNumber;
    uint32_t lastChanged; // Diff in ms since last change
} pt_spt_core_entry_t;

typedef struct pt_spt_core_table {
    int size;
    uint64_t lastUpdate; // timestamp
    pt_spt_core_entry_t** entries;
} pt_spt_core_table_t;

typedef struct protocol_spt {
    int id;
    uint64_t lastBeacon; // timestamp of last beacon
    // Tree info table
    pt_spt_tree_info_table_t* treeInfoTable;
    // neighborhood table
    pt_spt_neighborhood_table_t* neighborhoodTable;
    // backup ancestor table
    pt_spt_backup_ancestor_table_t* backupAncestorTable;
    // adjacency table
    pt_spt_adjacency_table_t* adjacencyTable;
    // core table
    pt_spt_core_table_t* coreTable;

    // CONFIGURABLES
    int heartbeatTime; // in seconds
} protocol_spt;

void spt_parse(hc_packet_t*, int, long, long, hypercast_t*);
hc_packet_t* spt_encode(void* msg, int, hypercast_t*);
void spt_maintenance(hypercast_t*);

protocol_spt* spt_protocol_from_config();

#endif

#ifndef TAG
#define TAG "HC_PROTOCOL_SPT"
#endif