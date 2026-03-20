#include "global_var.h"

cache_entry_t cache[CACHE_SIZE];
peer_t peers[MAX_PEERS];

uint8_t my_mac[6];
uint8_t cache_idx = 0;
uint8_t ESPNOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t ZEROS_MAC[6] = {0};
uint8_t ALL_NODES_DEST[6] = {0x99, 0x99, 0x99, 0x99, 0x99, 0x99};