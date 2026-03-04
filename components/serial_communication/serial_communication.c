#include "serial_communication.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "now_protocol.h"
#include <string.h>

#define UART_PORT UART_NUM_0
#define BUF_SIZE 1024

static const char *TAG = "serial";

void serial_init(void) {
  uart_config_t uart_config = {.baud_rate = 115200,
                               .data_bits = UART_DATA_8_BITS,
                               .parity = UART_PARITY_DISABLE,
                               .stop_bits = UART_STOP_BITS_1,
                               .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};

  uart_driver_install(UART_PORT, BUF_SIZE, 0, 0, NULL, 0);
  uart_param_config(UART_PORT, &uart_config);

  ESP_LOGI(TAG, "UART initialized");
}

void serial_task(void *pvParameters) {
  uint8_t data[BUF_SIZE];

  while (1) {
    const char *msg = "Hello from ESP32!\r\n";
    uart_write_bytes(UART_PORT, msg, strlen(msg));

    int len =
        uart_read_bytes(UART_PORT, data, BUF_SIZE - 1, pdMS_TO_TICKS(100));

    if (len > 0) {
      data[len] = 0;
      ESP_LOGI(TAG, "Received: %s", data);
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

bool send_to_serial(const espnow_msg_t *msg) {
  if (msg == NULL) {
    return false;
  }

  char buffer[512]; // increased because peer list can be large
  int len = 0;

  if (msg->type == MSG_TYPE_INFO) {

    // Base header (without data)
    len = snprintf(buffer, sizeof(buffer),
                   "%02X%02X%02X%02X%02X%02X;"
                   "%02X%02X%02X%02X%02X%02X;"
                   "%02X%02X%02X%02X%02X%02X;"
                   "%d;%lu;%u;%u;",
                   msg->src_mac[0], msg->src_mac[1], msg->src_mac[2],
                   msg->src_mac[3], msg->src_mac[4], msg->src_mac[5],

                   msg->dest_mac[0], msg->dest_mac[1], msg->dest_mac[2],
                   msg->dest_mac[3], msg->dest_mac[4], msg->dest_mac[5],

                   msg->origin_mac[0], msg->origin_mac[1], msg->origin_mac[2],
                   msg->origin_mac[3], msg->origin_mac[4], msg->origin_mac[5],

                   msg->rssi, (unsigned long)msg->msg_id, msg->ttl, msg->type);

    if (len <= 0 || len >= sizeof(buffer))
      return false;

    // Append peer list
    for (int i = 0; i < msg->peer_count; i++) {
      len += snprintf(
          buffer + len, sizeof(buffer) - len, "%02X%02X%02X%02X%02X%02X",
          msg->peer_macs[i][0], msg->peer_macs[i][1], msg->peer_macs[i][2],
          msg->peer_macs[i][3], msg->peer_macs[i][4], msg->peer_macs[i][5]);

      if (i < msg->peer_count - 1) {
        len += snprintf(buffer + len, sizeof(buffer) - len, ",");
      }
    }

    // End line
    len += snprintf(buffer + len, sizeof(buffer) - len, "\n");

  } else {

    // Normal DATA / ACK / DISCOVERY
    len = snprintf(buffer, sizeof(buffer),
                   "%02X%02X%02X%02X%02X%02X;"
                   "%02X%02X%02X%02X%02X%02X;"
                   "%02X%02X%02X%02X%02X%02X;"
                   "%d;%lu;%u;%u;%s\n",

                   msg->src_mac[0], msg->src_mac[1], msg->src_mac[2],
                   msg->src_mac[3], msg->src_mac[4], msg->src_mac[5],

                   msg->dest_mac[0], msg->dest_mac[1], msg->dest_mac[2],
                   msg->dest_mac[3], msg->dest_mac[4], msg->dest_mac[5],

                   msg->origin_mac[0], msg->origin_mac[1], msg->origin_mac[2],
                   msg->origin_mac[3], msg->origin_mac[4], msg->origin_mac[5],

                   msg->rssi, (unsigned long)msg->msg_id, msg->ttl, msg->type,
                   msg->data);
  }

  if (len <= 0 || len >= sizeof(buffer)) {
    return false;
  }

  int written = uart_write_bytes(UART_PORT, buffer, len);
  return (written == len);
}