/*
* This is where I'll define actual send and receive event loops
* Each will take the socket (already initalized) as an input
* They'll each be a spawned task, and be responsible for spawning
* any tasks that they rely on. Recall that if we're calling any sock
* commands here directly (as we do), that we don't want the processing
* of the packets to interfere. We'll need to spawn a task to do that
*/
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "esp_netif.h"
#include <lwip/netdb.h>

#include "hc_socket_interface.h"
#include "hypercast.h"
#include "hc_buffer.h"
#include "hc_engine.h"
#include "hc_lib.h"

#define MULTICAST_IPV4_ADDR "224.228.19.78"
#define MC_PORT 9472

#define SOCKET_SEND_DELAY 0.01
#define SOCKET_RECV_DELAY 0.01

// Number of messages received / second (Should be less than than 1000/SOCKET_RECV_DELAY)
#define FLUSH_MIN_MESSAGE_RATE 4
#define FLUSH_MESSAGE_INTERVAL 50
#define FLUSH_MAX_PACKETS 25

static const char* TAG = "HC_SOCKET_INTERFACE";

void hc_socket_interface_send_handler(void *pvParameters) {
    hypercast_t *hypercast = (hypercast_t *)pvParameters;

    int sock = hypercast->socket;

    // Now some constants that should really be config
    // char* mc_addr = "224.228.19.78";
    // int mc_port = 9472;

    // Then some re-usables
    hc_packet_t *packet;

    while (1) {
        // First read the buffer for data to send
        packet = NULL; // clean up
        // Now read
        packet = hc_pop_buffer(hypercast->sendBuffer);
        // If no data, pause then try again
        if (packet == NULL) { vTaskDelay(500 / portTICK_PERIOD_MS); continue; }

        ESP_LOGI(TAG, "Sending packet, send buffer length: %d", hypercast->sendBuffer->current_size);

        // Now it's time to send!
        // struct sockaddr_in to;
        // to.sin_family = AF_INET;
        // to.sin_port = htons(mc_port);
        // to.sin_addr.s_addr = inet_addr(mc_addr);
        
        struct sockaddr_in sdestv4 = {
            .sin_family = PF_INET,
            .sin_port = htons(MC_PORT),
        };
        // We know this inet_aton will pass because we did it above already
        inet_aton(MULTICAST_IPV4_ADDR, &sdestv4.sin_addr.s_addr);

        // First setup the target addr
        struct addrinfo hints = {
            .ai_flags = AI_PASSIVE,
            .ai_socktype = SOCK_DGRAM,
            .ai_family = AF_INET
        };
        struct addrinfo *faddr;
        char addrbuf[32] = { 0 };

        getaddrinfo(MULTICAST_IPV4_ADDR,
                    NULL,
                    &hints,
                    &faddr);

        ((struct sockaddr_in *)faddr->ai_addr)->sin_port = htons(MC_PORT);
        inet_ntoa_r(((struct sockaddr_in *)faddr->ai_addr)->sin_addr, addrbuf, sizeof(addrbuf)-1);
        ESP_LOGI(TAG, "Sending %d bytes to IPV4 multicast address %s:%d...", packet->size, addrbuf, MC_PORT);


        int res = sendto(sock, packet->data, packet->size, 0, faddr->ai_addr, faddr->ai_addrlen);
        // Now that we've sent the packet, we can free it
        // free_packet(packet); // Clearing this makes it impossible for the sendto to finish send async

        if (res < 0) {
            ESP_LOGE(TAG, "Error sending data: %d", res);
            continue;
        }
        ESP_LOGD(TAG, "Sent %d bytes to %s", res, "SOME ADDRESS");

        // This thread sleeps now to avoid flooding the port or overwriting its vibes
        vTaskDelay(SOCKET_SEND_DELAY / portTICK_PERIOD_MS);
        
    }
}

void hc_socket_interface_recv_handler(void *pvParameters) {
    hypercast_t *hypercast = (hypercast_t *)pvParameters;

    int sock = hypercast->socket;

    // Before we start receiving, let's lookup the local ip too
    // That way we can make sure not to receive any self casts :)
    esp_netif_t *netif = NULL;
    esp_netif_ip_info_t ipInfo; 
    
    netif = esp_netif_next(netif);
    esp_netif_get_ip_info(netif, &ipInfo);

    char localIpStr[32];
    int localIPLen = 32;

    // convert to str for str comp
    esp_ip4addr_ntoa(&ipInfo.ip, localIpStr, localIPLen);

    // Now some flush management
    int messageCounter = 0;
    int receiveStartTime = get_epoch();

    // Now start the receive event loop
    while (1) {
        ESP_LOGI(TAG, "Waiting for data...");

        static char recvbuf[1024];
        char raddr_name[32] = { 0 };

        // Before we look to receive a message, let's manage flush
        if (messageCounter >= FLUSH_MESSAGE_INTERVAL) {
            int currentTime = get_epoch();
            int timeDiff = currentTime - receiveStartTime; // In seconds
            if ((float)messageCounter/timeDiff > FLUSH_MIN_MESSAGE_RATE) {
                ESP_LOGI(TAG, "Flushing, msg/s is %f", (float)messageCounter/timeDiff);

                // Setup the flush
                struct timeval tv = {
                    .tv_sec = 1,
                    .tv_usec = 0,
                };
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(sock, &rfds);
                int flushCounter = 0;
                // do the flush
                while (select(sock+1, &rfds, NULL, NULL, &tv) > 0) {
                    recvfrom(sock, recvbuf, sizeof(recvbuf), 0, NULL, NULL);
                    flushCounter++;
                    if (flushCounter > FLUSH_MAX_PACKETS) {
                        ESP_LOGI(TAG, "Flush complete, %d packets flushed", flushCounter);
                        break;
                    }
                    // Maybe put a delay here?
                }
            }
            // Then reset the flush manager
            messageCounter = 0;
            receiveStartTime = currentTime;
        }

        struct sockaddr_storage raddr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(raddr);
        ESP_LOGI(TAG, "Receiving packet");
        int len = recvfrom(sock, recvbuf, sizeof(recvbuf)-1, 0,
                            (struct sockaddr *)&raddr, &socklen);
        ESP_LOGI(TAG, "Packet Received");
        if (len < 0) {
            ESP_LOGE(TAG, "multicast recvfrom failed: errno %d", errno);
            return; // This handler shouldn't return
        }
        if (raddr.ss_family == AF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&raddr)->sin_addr,
                        raddr_name, sizeof(raddr_name)-1);
        }
        // Before acknowledging the packet, check that it's from a valid address
        // This means that the address exists and differs from our own
        if (strcmp(raddr_name, localIpStr) == 0) {
            ESP_LOGI(TAG, "Received packet from self, ignoring");
            continue;
        }
        ESP_LOGI(TAG, "received %d bytes from %s:", len, raddr_name);
        // Then push the recvbuf into the hypercast buffer
        hc_push_buffer(hypercast->receiveBuffer, recvbuf, len);
        ESP_LOGI(TAG, "Unprocessed Buffer Length: %d", hypercast->receiveBuffer->current_size);

        // This thread sleeps now to avoid flooding the port or overwriting its vibes
        vTaskDelay(SOCKET_RECV_DELAY / portTICK_PERIOD_MS);
    }
}