#pragma once
#include "cache_types.h"
#include "peer_types.h"
#include <stdint.h>

extern cache_entry_t cache[CACHE_SIZE];
extern peer_t peers[MAX_PEERS];
extern uint8_t my_mac[6];
extern uint8_t cache_idx;
extern uint8_t ESPNOW_BROADCAST_MAC[6];
extern uint8_t ZEROS_MAC[6];
extern uint8_t ALL_NODES_DEST[6];