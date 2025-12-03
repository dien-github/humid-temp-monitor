idf_component_register(
    SRCS
        "utils.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_timer
        freertos
)

target_include_directories(${COMPONENT_LIB}
    PUBLIC include
)