#pragma once
#include "cJSON.h"
#include "esp_timer.h"
#include "espnow_types.h"
#include "global_var.h"

char *espnow_msg_to_json(const espnow_msg_t *msg);
bool get_dest_mac_from_json(const char *json, uint8_t mac[6]);
char *espnow_msg_to_json_info(const espnow_msg_t *msg);
char *data_msg_to_json(const data_t *data);