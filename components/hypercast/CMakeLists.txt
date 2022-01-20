idf_component_register(SRCS "hc_measure.c" "hc_lib.c" "hc_overlay.c" "hc_protocols.c" "hypercast.c" "hc_buffer.c" "hc_engine.c" "hc_socket_interface.c" "hc_protocols.c"
                    REQUIRES hypercast_protocols esp_http_client
                    INCLUDE_DIRS "include")
