#ifndef __HC_SOCKET_INTERFACE_H__
#define __HC_SOCKET_INTERFACE_H__

void hc_socket_interface_send_handler(void *pvParameters);
void hc_socket_interface_recv_handler(void *pvParameters);

#endif

#ifndef TAG
#define TAG "HC_SOCKET_INTERFACE"
#endif