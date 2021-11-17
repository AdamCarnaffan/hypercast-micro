idf_component_register(SRCS "network_station_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES hypercast
                    PRIV_REQUIRES nvs_flash app_update)
