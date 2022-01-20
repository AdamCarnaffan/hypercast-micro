
#include <string.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_client.h"

#include "hc_measure.h"
#include "hc_buffer.h"

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    return ESP_OK;
}

void log_nodestate(hypercast_t* hypercast) {
    // Init buffer
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

    // Setup config
    esp_http_client_config_t config = {
        .host = "192.168.122.100",
        .path = "/log/",
        .query = "esp",
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer,        // Pass address of local buffer to get response
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Now do the post request
    const char *post_data = "{\"field1\":\"value1\"}";
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

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
    return;
}