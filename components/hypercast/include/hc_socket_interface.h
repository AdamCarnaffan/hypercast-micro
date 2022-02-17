#ifndef __HC_SOCKET_INTERFACE_H__
#define __HC_SOCKET_INTERFACE_H__

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#include "esp_log.h"

void hc_socket_interface_send_handler(void *pvParameters);
void hc_socket_interface_recv_handler(void *pvParameters);

#endif