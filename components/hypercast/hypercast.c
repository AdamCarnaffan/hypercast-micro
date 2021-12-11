
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/err.h"

#include "hypercast.h"
#include "hc_engine.h"
#include "hc_socket_interface.h"
#include "hc_protocols.h"

#include "spt.h"

hypercast_t* hypercast;

void hc_init(void *pvParameters) {
    int sock = *((int *)pvParameters);
    ESP_LOGI(TAG, "Socket Ready");

    // Build the hypercast state machine
    hypercast = malloc(sizeof(hypercast_t));

    // Install heap-allocated pointers
    hypercast->receiveBuffer = malloc(sizeof(hc_buffer_t));
    hypercast->sendBuffer = malloc(sizeof(hc_buffer_t));

    // Allocate memory & set initial values
    hypercast->socket = sock;
    hc_allocate_buffer(hypercast->receiveBuffer, HC_BUFFER_SIZE);
    hc_allocate_buffer(hypercast->sendBuffer, HC_BUFFER_SIZE);
    hc_install_config(hypercast);

    // Run send receive handlers
    xTaskCreate(hc_socket_interface_recv_handler, "HYPERCAST_receive_handler", 4096, hypercast, 5, NULL);
    xTaskCreate(hc_socket_interface_send_handler, "HYPERCAST_send_handler", 4096, hypercast, 5, NULL);
    ESP_LOGI(TAG, "Handlers Started");

    // Start the engine
    ESP_LOGI(TAG, "Buffer Processor Ready");
    hc_engine_handler(hypercast); // This runs the for loop on this thread forever :)
}

void hc_install_config(hypercast_t *hypercast) {
    ESP_LOGI(TAG, "Installing Config...");

    // Usually we'd read config here, but for now just set the default values
    hypercast->protocol = resolve_protocol_to_install(HC_PROTOCOL_SPT); // usually config would feed in here instead
    ESP_LOGI(TAG, "Protocol: %d", (int)((hc_protocol_shell_t*)(hypercast->protocol))->id);
    return;
}
