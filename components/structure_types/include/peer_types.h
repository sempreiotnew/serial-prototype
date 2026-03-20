#pragma once

#include <stdbool.h>
#include <stdint.h>
#define MAX_PEERS 10

typedef struct {
  uint8_t mac[6];
  bool active;
  int rssi;
  int64_t last_seen;
} peer_t;