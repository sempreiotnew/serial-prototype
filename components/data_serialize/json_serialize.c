#include "data_serialize.h"
#include <stdio.h>

/* Convert MAC → string */
static void mac_to_str(const uint8_t *mac, char *out) {
  sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
          mac[4], mac[5]);
}

/* Add MAC field to JSON */
static void json_add_mac(cJSON *obj, const char *key, const uint8_t *mac) {
  char buf[18];
  mac_to_str(mac, buf);
  cJSON_AddStringToObject(obj, key, buf);
}

char *espnow_msg_to_json(const espnow_msg_t *msg) {
  cJSON *root = cJSON_CreateObject();

  // MAC addresses
  json_add_mac(root, "src_mac", msg->src_mac);
  json_add_mac(root, "dest_mac", msg->dest_mac);
  json_add_mac(root, "origin_mac", msg->origin_mac);

  // basic fields
  cJSON_AddNumberToObject(root, "rssi", msg->rssi);
  cJSON_AddNumberToObject(root, "msg_id", msg->msg_id);
  cJSON_AddNumberToObject(root, "ttl", msg->ttl);
  cJSON_AddNumberToObject(root, "type", msg->type);

  // bool
  cJSON_AddBoolToObject(root, "forwarded", msg->forwarded);

  // data payload
  cJSON_AddStringToObject(root, "data", msg->data);

  // peer info
  cJSON_AddNumberToObject(root, "peer_count", msg->peer_count);

  cJSON *peer_array = cJSON_CreateArray();

  for (int i = 0; i < msg->peer_count; i++) {
    char mac[18];
    mac_to_str(msg->peer_macs[i], mac);
    cJSON_AddItemToArray(peer_array, cJSON_CreateString(mac));
  }

  cJSON_AddItemToObject(root, "peer_macs", peer_array);

  char *json = cJSON_PrintUnformatted(root);

  cJSON_Delete(root);

  return json;
}