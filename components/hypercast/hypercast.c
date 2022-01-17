
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
    hc_engine_handler(hypercast); // This runs the for loop on this thread forever :)
}

void hc_install_config(hypercast_t *hypercast) {
    ESP_LOGI(TAG, "Installing Config...");

    // Now let's generate a source logical address for the node
    uint32_t sourceLogicalGenerated = 0;
    while (sourceLogicalGenerated == 0) {
        sourceLogicalGenerated = esp_random() % 999;
    }

    // Before looking at protocol, let's use the interface to setup the senderTable
    hypercast->senderTable = malloc(sizeof(hc_sender_table_t));
    hypercast->senderTable->size = 1;
    hypercast->senderTable->sourceAddressLogical = sourceLogicalGenerated;
    // Setup the table
    hypercast->senderTable->entries = malloc(sizeof(hc_sender_entry_t*)*hypercast->senderTable->size);
    // Setup the first entry
    hypercast->senderTable->entries[0] = malloc(sizeof(hc_sender_entry_t));
    hypercast->senderTable->entries[0]->type = 1;
    hypercast->senderTable->entries[0]->hash = 64935;
    hypercast->senderTable->entries[0]->addressLength = 6;
    hypercast->senderTable->entries[0]->address = malloc(sizeof(hc_ipv4_addr_t));
    hypercast->senderTable->entries[0]->address->addr[0] = 224;
    hypercast->senderTable->entries[0]->address->addr[1] = 228;
    hypercast->senderTable->entries[0]->address->addr[2] = 19;
    hypercast->senderTable->entries[0]->address->addr[3] = 78;
    hypercast->senderTable->entries[0]->port = 9472;

    // Usually we'd read config here, but for now just set the default values
    hypercast->protocol = resolve_protocol_to_install(HC_PROTOCOL_SPT, hypercast->senderTable->sourceAddressLogical); // usually config would feed in here instead
    ESP_LOGI(TAG, "Protocol: %d", (int)((hc_protocol_shell_t*)(hypercast->protocol))->id);

    // Finish by installing a callback
    hypercast->callback = hc_callback_handler;
    return;
}

void hc_callback_handler(char* data, int length) {
    ESP_LOGI(TAG, "Callback Handled for %.*s", length, data);
    return;
}


// HASHES USE A GENERAL HASH FUNCTION AND HERE'S HOW THAT SHAKES DOWN FOR THE OVERLAY
//  public int setOverlayHash() {
//         Vector hashPropertyVec = getListOfPropertyNames(this.propertyDoc, HyperCastConfig.OVERLAYHASH_PROPERTY_NAME);
//         String s = "";

//         for (int i = 0; i < hashPropertyVec.size(); i++) {
//             String value = getStringProperty((String) hashPropertyVec.elementAt(i));

//             if (value != null) {
//                 if (s.equals("")) {
//                     s = s + (String) hashPropertyVec.elementAt(i) + "=" + value;
//                 } else {
//                     s = s + "&" + (String) hashPropertyVec.elementAt(i) + "=" + value;
//                 }
//             }
//         }
//         return hash(s.getBytes());
//     }

//     /**
//      * Generates a hash of a byte array for the setOverlayHash function. I could
//      * just use Java's hashCode for a string object, but I wanted something that
//      * would be available to any other implementation language.
//      */
//     public static int hash(byte[] array) {
//         int h = 0;
//         for (int i = 0; i < array.length; i++) {
//             byte upperByte = (byte) ((h >>> 24) & 0xFF);
//             int leftShiftValue = ((upperByte ^ array[i]) & 0x07) + 1;
//             h = ((h << leftShiftValue) ^ ((upperByte ^ array[i]) & 0xFF));
//         }

//         return h;
//     }