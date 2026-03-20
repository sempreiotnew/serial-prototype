idf_component_register(
    SRCS "json_serialize.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        structure_types
        json
        esp_timer
)
