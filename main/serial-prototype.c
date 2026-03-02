#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "now_protocol.h"
#include "serial_communication.h"
void app_main(void) {
  esp_log_level_set("*", ESP_LOG_NONE); // disable all logs
  serial_init();

  //   xTaskCreate(serial_task,   // task function
  //               "serial_task", // name
  //               4096,          // stack size
  //               NULL,          // parameters
  //               5,             // priority
  //               NULL           // task handle
  //   );

  run_now();
}