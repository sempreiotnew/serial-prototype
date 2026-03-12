idf_component_register(
    SRCS "json_serialize.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        protocol_types
        json
)
