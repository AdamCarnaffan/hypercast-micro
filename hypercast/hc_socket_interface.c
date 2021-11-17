/*
* This is where I'll define actual send and receive event loops
* Each will take the socket (already initalized) as an input
* They'll each be a spawned task, and be responsible for spawning
* any tasks that they rely on. Recall that if we're calling any sock
* commands here directly (as we do), that we don't want the processing
* of the packets to interfere. We'll need to spawn a task to do that
*/
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/err.h"

#include "hc_buffer.h"
#include "hc_engine.c"

#ifndef TAG
#define TAG "HC_SOCKET_INTERFACE"
#endif

static void hc_socket_interface_send_handler(void *pvParameters) {
    // unimplemented.
    while (1) {
        vTaskDelay(10000);
    }
}

static void hc_socket_interface_recv_handler(void *pvParameters) {
    int sock = *((int *)pvParameters);

    // This setup doesn't belong here, but is fine until we implement send
    hc_buffer_t hc_buffer;
    hc_allocate_buffer(&hc_buffer, 100); // HC_BUFFER_SIZE should come from hc config at some point

    // Before starting socket receive, let's spawn the SEPARATE task
    // responsable for handling input from this buffer!
    xTaskCreate(hc_engine_handler, "HYPERCAST Engine", 4096, &hc_buffer, 5, NULL);

    // Now start the receive event loop
    while (1) {
        ESP_LOGI(TAG, "Waiting for data...");
        // Receive data
        // Sample SPT Message: \x33\x00\x41\x00\x15\x67\xb2\x03\x02\xff\x01\xd2\x83\x06\xc0\xa8\x02\x64\xc1\x97\x08\xff\x41\xfd\xa7\x06\xe0\xe4\x13\x4e\x25\x00\xb9\x00\x00\x01\x18\x00\x00\x00\x21\x00\x00\x00\x21\x00\x00\x00\x01\x00\x00\x01\x7c\xd9\xac\x12\xde\x00\x00\x00\x01\x00\x00\x00\x21\x87\x27\x10
        // Sample Overlay Message: \xd0\x31\x00\x00\x00\x00\x15\x00\xfe\x02\x04\x00\x00\x01\x52\x00\x00\x01\x52\x03\x01\x0b\x48\x65\x6c\x6c\x6f\x20\x57\x6f\x72\x6c\x64\x00\x01\x04\x00\x00\x01\x52
        // Send them with echo -n -e "HEX HERE" | nc -u 192.168.2.69 9472

        char recvbuf[1024];
        char raddr_name[32] = { 0 };

        struct sockaddr_storage raddr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(raddr);
        int len = recvfrom(sock, recvbuf, sizeof(recvbuf)-1, 0,
                            (struct sockaddr *)&raddr, &socklen);
        if (len < 0) {
            ESP_LOGE(TAG, "multicast recvfrom failed: errno %d", errno);
            return; // This handler shouldn't return
        }
        if (raddr.ss_family == AF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&raddr)->sin_addr,
                        raddr_name, sizeof(raddr_name)-1);
        }
        ESP_LOGI(TAG, "received %d bytes from %s:", len, raddr_name);
        ESP_LOGI(TAG, "%.*s", len, recvbuf);
        // Then push the recvbuf into the hypercast buffer
        hc_push_buffer(&hc_buffer, recvbuf, len);
    }
}