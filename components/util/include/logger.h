#pragma once

#include "espnow_types.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

const char *msg_type_to_str(uint8_t type);
bool mac_equal(const uint8_t *a, const uint8_t *b);

static inline void mac_to_str_buf(const uint8_t *mac, char *out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
}

static inline const char *mac_to_str(const uint8_t *mac) {
  static char buf[4][18];
  static uint8_t idx = 0;

  idx = (idx + 1) % 4;
  mac_to_str_buf(mac, buf[idx]);
  return buf[idx];
}