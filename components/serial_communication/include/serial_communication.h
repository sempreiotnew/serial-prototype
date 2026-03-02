#pragma once
#include "now_protocol.h"
#include <stdbool.h>

void serial_init(void);
void serial_task(void *pvParameters);
bool send_to_serial(const espnow_msg_t *msg);