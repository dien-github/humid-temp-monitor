idf_component_register(
    SRCS
        "app_config.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        nvs_flash
)

target_include_directories(${COMPONENT_LIB}
    PUBLIC include
)
