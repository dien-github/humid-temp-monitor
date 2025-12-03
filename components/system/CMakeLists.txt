idf_component_register(
    SRCS
        "system_task.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
        esp_timer
        app_config
        sensor
        output
        network
)

target_include_directories(${COMPONENT_LIB}
    PUBLIC include
)