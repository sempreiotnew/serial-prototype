idf_component_register(
    SRCS "serial-prototype.c"
    INCLUDE_DIRS "."
    REQUIRES 
        serial_communication 
        now_protocol
)