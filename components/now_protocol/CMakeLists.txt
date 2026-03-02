idf_component_register(
    SRCS "now_protocol.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        driver
        serial_communication
)
