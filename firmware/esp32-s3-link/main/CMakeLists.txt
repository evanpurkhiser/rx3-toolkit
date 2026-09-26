idf_component_register(SRCS "rx3_link_bridge.c" "bridge_config.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_event esp_partition esp_rom esp_system esp_wifi nvs_flash)
