#pragma once

#include <stdbool.h>
#include <stdint.h>

void run_now();

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
} __attribute__((packed)) espnow_msg_t;
