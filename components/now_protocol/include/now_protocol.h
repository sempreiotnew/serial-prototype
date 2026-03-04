#pragma once

#include <stdbool.h>
#include <stdint.h>

void run_now();

/* ===================== PROTOCOL ===================== */

typedef enum {
  MSG_TYPE_DISCOVERY = 0x01,
  MSG_TYPE_DATA = 0x02,
  MSG_TYPE_ACK = 0x03,
  MSG_TYPE_INFO = 0x04,
} msg_type_t;

/* ===================== MESSAGE ===================== */

typedef struct {
  uint8_t src_mac[6]; // immediate sender
  uint8_t dest_mac[6];
  uint8_t origin_mac[6]; // original creator
  int8_t rssi;
  uint32_t msg_id;
  uint8_t ttl;
  uint8_t type;
  char data[10];
  bool forwarded;

  // ---- NEW FIELDS FOR INFO ----
  uint8_t peer_count;
  uint8_t peer_macs[10][6];

} __attribute__((packed)) espnow_msg_t;
