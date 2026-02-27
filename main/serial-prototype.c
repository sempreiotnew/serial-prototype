#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define UART_PORT UART_NUM_0
#define BUF_SIZE 1024

static const char *TAG = "UART";

void app_main(void) {
  uart_config_t uart_config = {.baud_rate = 115200,
                               .data_bits = UART_DATA_8_BITS,
                               .parity = UART_PARITY_DISABLE,
                               .stop_bits = UART_STOP_BITS_1,
                               .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};

  uart_driver_install(UART_PORT, BUF_SIZE, 0, 0, NULL, 0);
  uart_param_config(UART_PORT, &uart_config);

  ESP_LOGI(TAG, "UART initialized");

  uint8_t data[BUF_SIZE];

  while (1) {

    // Send hello every 2 seconds
    const char *msg = "Hello from ESP32!\r\n";
    uart_write_bytes(UART_PORT, msg, strlen(msg));

    // Read data
    int len =
        uart_read_bytes(UART_PORT, data, BUF_SIZE - 1, pdMS_TO_TICKS(100));

    if (len > 0) {
      data[len] = 0;
      ESP_LOGI(TAG, "Received: %s", data);
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}