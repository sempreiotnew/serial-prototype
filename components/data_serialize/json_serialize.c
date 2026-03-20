#include "data_serialize.h"

#include <stdio.h>
#include <string.h>
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
  // cJSON_AddNumberToObject(root, "peer_count", msg->peer_count);

  // cJSON *peer_array = cJSON_CreateArray();

  // for (int i = 0; i < MAX_PEERS; i++) {
  //   char mac[18];
  //   mac_to_str(peers[i].mac, mac);
  //   cJSON_AddItemToArray(peer_array, cJSON_CreateString(mac));
  // }

  // cJSON_AddItemToObject(root, "peer_macs", peer_array);

  char *json = cJSON_PrintUnformatted(root);

  cJSON_Delete(root);

  return json;
}

char *data_msg_to_json(const data_t *data) {
  cJSON *root = cJSON_CreateObject();

  // MAC addresses
  json_add_mac(root, "src_mac", data->src_mac);
  json_add_mac(root, "dest_mac", data->dest_mac);
  json_add_mac(root, "origin_mac", data->origin_mac);

  // basic fields
  cJSON_AddBoolToObject(root, "alarm", data->alarm);
  cJSON_AddNumberToObject(root, "event_id", data->event_id);

  // bool
  cJSON_AddStringToObject(root, "name", data->name);

  // // data payload
  // cJSON_AddStringToObject(root, "data", msg->data);

  char *json = cJSON_PrintUnformatted(root);

  cJSON_Delete(root);

  return json;
}

char *espnow_msg_to_json_info(const espnow_msg_t *msg) {
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
  cJSON_AddBoolToObject(root, "is_root", msg->is_root);

  // peer info
  cJSON *peer_array = cJSON_CreateArray();

  for (int i = 0; i < MAX_PEERS; i++) {

    if (!peers[i].active)
      continue;

    cJSON *peer_obj = cJSON_CreateObject();

    char mac[18];
    mac_to_str(peers[i].mac, mac);

    cJSON_AddStringToObject(peer_obj, "mac", mac);
    cJSON_AddNumberToObject(peer_obj, "rssi", peers[i].rssi);
    cJSON_AddBoolToObject(peer_obj, "active", peers[i].active);

    int64_t now = esp_timer_get_time();
    int64_t last_ms = (now - peers[i].last_seen) / 1000;

    cJSON_AddNumberToObject(peer_obj, "last_seen", last_ms);

    cJSON_AddItemToArray(peer_array, peer_obj);
  }

  cJSON_AddNumberToObject(root, "peer_count", cJSON_GetArraySize(peer_array));
  cJSON_AddItemToObject(root, "peers", peer_array);

  char *json = cJSON_PrintUnformatted(root);

  cJSON_Delete(root);
  return json;
}

bool get_dest_mac_from_json(const char *json, uint8_t mac[6]) {
  cJSON *root = cJSON_Parse(json);
  if (!root) {
    return false;
  }

  cJSON *dest = cJSON_GetObjectItem(root, "dest_mac");
  if (!cJSON_IsString(dest)) {
    cJSON_Delete(root);
    return false;
  }

  sscanf(dest->valuestring, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1],
         &mac[2], &mac[3], &mac[4], &mac[5]);

  cJSON_Delete(root);
  return true;
}