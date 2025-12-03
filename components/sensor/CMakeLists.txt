# components/sensor/CMakeLists.txt
idf_component_register(
    SRCS
        "sensor_dht.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_timer
        app_config
)

target_include_directories(${COMPONENT_LIB}
    PUBLIC include
)