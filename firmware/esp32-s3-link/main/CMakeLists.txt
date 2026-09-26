idf_component_register(SRCS "rx3_link_bridge.c" "bridge_config.c"
                            "config_protocol.c" "wifi_credentials.c"
                       INCLUDE_DIRS "." "../include"
                       PRIV_REQUIRES esp_event esp_partition esp_rom esp_system esp_wifi nvs_flash)
