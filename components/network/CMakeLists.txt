idf_component_register(
    SRCS
        "app_wifi.c"
        "app_mqtt.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        mqtt
        freertos
        app_config
)

target_include_directories(${COMPONENT_LIB}
    PUBLIC include
)