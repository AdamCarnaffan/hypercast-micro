// Before importing, define ESP App tag
#define TAG "wifi station"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

// My includes
#include "network_station_main.h"
#include "hypercast.h"

// KEY WORD: SOCKET
// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/lwip.html

// Try Disable sleep
// https://arduino.stackexchange.com/questions/39957/esp8266-udp-multicast-doesnt-receive-packets

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// static const char *TAG = "wifi station";

static int s_retry_num = 0;

static void hc_socket_init();

// This code handles connection-related events
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    // Connection Process Events
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < NETWORK_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
        return;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        // This is where we can init HyperCast!!
        xTaskCreate(hc_socket_init, "hc_socket_init", 4096, &event->ip_info.ip, 5, NULL);
        return;
    }
}

static void hc_socket_init(void* pvParameters) {
    // esp_ip4_addr_t* delegated_ip = (esp_ip4_addr_t*)pvParameters;
    // Start by creating a socket
    int err;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Error creating socket: %d", sock);
        return;
    }
    // We'll use Reuse address, even though I don't think it's necessary
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        ESP_LOGE(TAG, "Error setting socket options: %d", sock);
        return;
    }

    // Now let's setup a local address as well as a multicast group mreq
    struct sockaddr_in local_addr = { 0 };
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(9472);
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Now let's setup the multicast group membership to finish
    struct ip_mreq mc_group = { 0 };
    // struct in_addr mc_addr = { 0 };
    // Now start setting the values
    err = inet_aton("224.228.19.78", &mc_group.imr_multiaddr.s_addr);
    if (err < 0) {
        ESP_LOGE(TAG, "Error setting multicast address: %d", err);
        return;
    }
    // Set the interface address as well
    mc_group.imr_interface.s_addr = IPADDR_ANY; // inet_addr("192.168.2.69");
    ESP_LOGI(TAG, "Configured IPV4 Multicast address %s", inet_ntoa(mc_group.imr_multiaddr.s_addr));
    // Do a final check that the multicast address is a valid one
    if (!IP_MULTICAST(ntohl(mc_group.imr_multiaddr.s_addr))) {
        ESP_LOGE(TAG, "Multicast address is likely not valid: %d", err);
        // We don't quit tho, it is possible it works
    }

    err = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mc_group, sizeof(mc_group));
    if (err < 0) {
        ESP_LOGE(TAG, "Error adding membership: %d", err);
        return;
    }

    err = bind(sock, (struct sockaddr*) &local_addr, sizeof(local_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Error binding socket: %d", err);
        return;
    }

    // Before mutlicast setup, set multicast TTL
    uint8_t ttl = 1;
    err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    if (err < 0) {
        ESP_LOGE(TAG, "Error setting socket option TTL: %d", err);
        return;
    }

    // We'll also need to set the multicast interface to listen for multicast packets
    uint8_t loopback_if = 0;
    err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback_if, sizeof(loopback_if));
    if (err < 0) {
        ESP_LOGE(TAG, "Error setting socket option LOOPBACK: %d", err);
        return;
    }

    // And then set the multicast source interface
    // err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &local_addr, sizeof(local_addr));
    // if (err < 0) {
    //     ESP_LOGE(TAG, "Error setting socket interface: %d", err);
    //     return;
    // }

    

    // Then before we include membership, let's assign a source member interface
    // mc_addr.s_addr = inet_addr("192.168.2.69"); //htonl(INADDR_ANY);
    // err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &mc_addr, sizeof(mc_addr));
    // if (err < 0) {
    //     ESP_LOGE(TAG, "Error setting socket group multicast source interface: %d", err);
    //     return;
    // }

    

    // Setup binding
    // struct sockaddr_in addr;
    // addr.sin_family = AF_INET;
    // addr.sin_port = htons(9472);
    // addr.sin_addr.s_addr = htonl(INADDR_ANY);
    // Finish the binding
    // int err = bind(sock, (struct sockaddr*) &local_addr, sizeof(local_addr));
    // if (err < 0) {
    //     ESP_LOGE(TAG, "Error binding socket: %d", err);
    //     return;
    // }

    // Now do a bit of hypercast related setup
    // hc_buffer_t hc_buffer;
    // hc_allocate_buffer(&hc_buffer, HC_BUFFER_SIZE);
    // Init some kind of engine here with send and receive buffers?

    // Before we begin the hypercast engine, wait 3 seconds for the multicast to be setup
    // vTaskDelay(3000 / portTICK_PERIOD_MS);

    // Now we finish socket init by starting the hypercast engine!
    xTaskCreate(hc_init, "HYPERCAST_engine_handler", 8192, (void *)&sock, 5, NULL);

    // We finish socket init by deploying our hypercast functionalities
    // xTaskCreate(hc_socket_interface_recv_handler, "HYPERCAST_receive_handler", 4096, (void *)&sock, 5, NULL);
    // xTaskCreate(hc_socket_interface_send_handler, "HYPERCAST_send_handler", 4096, (void *)&sock, 5, NULL);
    while (1) { vTaskDelay(10000 / portTICK_PERIOD_MS); } // I don't know how to deal with this 
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = NETWORK_WIFI_SSID,
            .password = NETWORK_WIFI_PASSWORD,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    ESP_LOGI(TAG, "print.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 NETWORK_WIFI_SSID, NETWORK_WIFI_PASSWORD);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 NETWORK_WIFI_SSID, NETWORK_WIFI_PASSWORD);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
}
