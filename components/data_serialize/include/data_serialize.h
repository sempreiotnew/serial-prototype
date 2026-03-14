#include "cJSON.h"
#include "espnow_types.h"

char *espnow_msg_to_json(const espnow_msg_t *msg);
bool get_dest_mac_from_json(const char *json, uint8_t mac[6]);