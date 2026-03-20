#pragma once

#include <stdint.h>
#define CACHE_SIZE 512
typedef struct {
  uint8_t src_mac[6];
  uint8_t dest_mac[6];
  uint8_t origin_mac[6];
  uint32_t msg_id;
  uint8_t type;
  char data[10];
} cache_entry_t;
