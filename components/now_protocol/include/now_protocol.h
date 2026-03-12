#pragma once

#include <stdbool.h>
#include <stdint.h>

void run_now();

#define DATA_SIZE 512
#define MAX_PEERS 10

typedef enum {
  MSG_TYPE_DISCOVERY = 0x01,
  MSG_TYPE_DATA = 0x02,
  MSG_TYPE_FWD = 0x03,
  MSG_TYPE_ACK = 0x04,
  MSG_TYPE_INFO = 0x05,
  MSG_TYPE_PING = 0x06,
  MSG_TYPE_PONG = 0x07
} msg_type_t;

typedef struct {
  uint8_t src_mac[6];
  uint8_t dest_mac[6];
  uint8_t origin_mac[6];
  int8_t rssi;
  uint32_t msg_id;
  uint8_t ttl;
  uint8_t type;
  char data[DATA_SIZE];
  bool forwarded;

  uint8_t peer_count;
  uint8_t peer_macs[MAX_PEERS][6];

} __attribute__((packed)) espnow_msg_t;