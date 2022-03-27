
#include <string.h>
#include "esp_netif.h"

#include "hc_measure.h"
#include "hc_buffer.h"
#include "hc_lib.h"
#include "spt.h"

static const char* TAG = "HC_MEASURE";

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    return ESP_OK;
}

void hc_measure_handler(void *pvParameters) {
    hypercast_t *hypercast = (hypercast_t *)pvParameters;

    while (1) {
        // We'll always just take a sleep
        vTaskDelay(MEASUREMENT_INTERVAL / portTICK_PERIOD_MS);

        // And after our sleep we try the measure
        log_nodestate(hypercast);
    }
}

void log_nodestate(hypercast_t* hypercast) {

    // Init buffer
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

    // If I want CPU usage, use https://github.com/Carbon225/esp32-perfmon
    // Read resources
    int freeHeapSize = esp_get_free_heap_size();

    ESP_LOGI(TAG, "Free Heap: %d / %d", freeHeapSize, MAX_MEMORY_AVAILABLE);

    // Setup config
    esp_http_client_config_t config = {
        .host = "192.168.122.100",
        .port = 8000,
        .path = "/log/",
        .query = "esp",
        .max_redirection_count = 5,
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer,        // Pass address of local buffer to get response
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Now do the post request
    char data[HC_BUFFER_DATA_MAX]; // Temporary buffer of max size to shove data into
    int dataSize = 0;
    int i;

    // Build the request data
    // We'll meassure:
    // 0. Node Type (C or Java)
    write_bytes(data, 1, 4, 0, HC_BUFFER_DATA_MAX);
    // 1. Node protocol id
    write_bytes(data, ((hc_protocol_shell_t *)hypercast->protocol)->id, 4, 4, HC_BUFFER_DATA_MAX);
    // 2. Timestamp
    write_bytes(data, get_epoch(), 32, 8, HC_BUFFER_DATA_MAX);

    dataSize = 5; // 40 bits is 5 bytes
    
    // Now assume we're using SPT (need to improve if we use other protocols)
    protocol_spt* spt = (protocol_spt *)hypercast->protocol;
    // 3. Node neighbor table
    // First we write the number of entries
    write_bytes(data, spt->neighborhoodTable->size, 8, 40, HC_BUFFER_DATA_MAX);
    dataSize += 1;
    // Then start writing entries
    for (i=0;i<spt->neighborhoodTable->size;i++) {
        // Write the entry
        write_bytes(data, spt->neighborhoodTable->entries[i]->neighborId, 16, dataSize*8, HC_BUFFER_DATA_MAX);
        write_bytes(data, spt->neighborhoodTable->entries[i]->physicalAddress, 32, dataSize*8 + 16, HC_BUFFER_DATA_MAX);
        write_bytes(data, spt->neighborhoodTable->entries[i]->rootId, 16, dataSize*8 + 48, HC_BUFFER_DATA_MAX);
        write_bytes(data, spt->neighborhoodTable->entries[i]->cost, 32, dataSize*8 + 64, HC_BUFFER_DATA_MAX);
        write_bytes(data, spt->neighborhoodTable->entries[i]->pathMetric, 32, dataSize*8 + 96, HC_BUFFER_DATA_MAX);
        write_bytes(data, spt->neighborhoodTable->entries[i]->timestamp/1000, 32, dataSize*8 + 128, HC_BUFFER_DATA_MAX);
        write_bytes(data, spt->neighborhoodTable->entries[i]->isAncestor, 8, dataSize*8 +160, HC_BUFFER_DATA_MAX);
        // Update the dataSize
        dataSize += 2+4+2+4+4+8+1;
    }
    // 4. Node adjacency table
    // First we write the number of entries
    write_bytes(data, spt->adjacencyTable->size, 8, dataSize*8, HC_BUFFER_DATA_MAX);
    dataSize += 1;
    // Then start writing entries
    for (i=0;i<spt->adjacencyTable->size;i++) {
        // Write the entry
        // uint32_t id;
        write_bytes(data, spt->adjacencyTable->entries[i]->id, 32, dataSize*8, HC_BUFFER_DATA_MAX);
        // uint8_t quality;
        write_bytes(data, spt->adjacencyTable->entries[i]->quality, 8, dataSize*8 + 32, HC_BUFFER_DATA_MAX);
        // uint64_t timestamp;
        write_bytes(data, spt->adjacencyTable->entries[i]->timestamp/1000, 32, dataSize*8 + 40, HC_BUFFER_DATA_MAX);
        // Update the dataSize
        dataSize += 4+1+4;
    }
    // 5. Node treeInfoTable
    // This one doesn't need size because the props only exist once
    // uint16_t id;
    write_bytes(data, spt->treeInfoTable->id, 16, dataSize*8, HC_BUFFER_DATA_MAX);
    dataSize += 2;
    // uint32_t physicalAddress;
    write_bytes(data, spt->treeInfoTable->physicalAddress, 32, dataSize*8, HC_BUFFER_DATA_MAX);
    dataSize += 4;
    // uint16_t rootId;
    write_bytes(data, spt->treeInfoTable->rootId, 16, dataSize*8, HC_BUFFER_DATA_MAX);
    dataSize += 2;
    // uint32_t ancestorId;
    write_bytes(data, spt->treeInfoTable->ancestorId, 32, dataSize*8, HC_BUFFER_DATA_MAX);
    dataSize += 4;
    // uint32_t cost;
    write_bytes(data, spt->treeInfoTable->cost, 32, dataSize*8, HC_BUFFER_DATA_MAX);
    dataSize += 4;
    // uint32_t pathMetric;
    write_bytes(data, spt->treeInfoTable->pathMetric, 32, dataSize*8, HC_BUFFER_DATA_MAX);
    dataSize += 4;
    // uint32_t sequenceNumber;
    write_bytes(data, spt->treeInfoTable->sequenceNumber, 32, dataSize*8, HC_BUFFER_DATA_MAX);
    dataSize += 4;
    // 6. RAM usage
    write_bytes(data, freeHeapSize, 32, dataSize*8, HC_BUFFER_DATA_MAX);
    dataSize += 4;
    write_bytes(data, MAX_MEMORY_AVAILABLE, 32, dataSize*8, HC_BUFFER_DATA_MAX);
    dataSize += 4;

    ESP_LOGI(TAG, "Measure written to bytestream");

    // Put it together
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_post_field(client, data, dataSize);

    // Execute
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Nodestate recorded");
    
    // Cleanup
    esp_http_client_cleanup(client);
    return;
}