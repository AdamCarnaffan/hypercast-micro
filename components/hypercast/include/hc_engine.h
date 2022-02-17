#ifndef __HC_ENGINE_H__
#define __HC_ENGINE_H__

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#include "esp_log.h"
#include "hypercast.h"
#include "hc_buffer.h"

#define HC_OVERLAY_PACKET_LENGTH 14
#define HC_PROTOCOL_PACKET_LENGTH 35

void hc_engine_handler(hypercast_t *hypercast);
void hc_forward(hc_packet_t*, hypercast_t*);

#endif