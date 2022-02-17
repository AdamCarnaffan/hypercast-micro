#ifndef __HC_MEASURE_H__
#define __HC_MEASURE_H__

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#include "esp_log.h"
#include "hypercast.h"
#include "esp_http_client.h"

#define SEND_MEASURES 1
#define MEASUREMENT_INTERVAL 5000
#define MAX_HTTP_OUTPUT_BUFFER 1024

#define MAX_MEMORY_AVAILABLE 320000

esp_err_t _http_event_handler(esp_http_client_event_t *);
void hc_measure_handler(void *);
void log_nodestate(hypercast_t*);

#endif