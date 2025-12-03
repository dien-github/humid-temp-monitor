idf_component_register(
    SRCS
        "app_output.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        freertos
        app_config
)

target_include_directories(${COMPONENT_LIB}
    PUBLIC include
)