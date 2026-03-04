#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "driver/gpio.h"
#include "nvs_flash.h"

#include "now_protocol.h"
#include "serial_communication.h"

/* ===================== CONFIG ===================== */

#define TAG "now_protocol.c"

#define LED_GPIO GPIO_NUM_2
#define BUTTON_GPIO GPIO_NUM_0

#define ESPNOW_CHANNEL 6
#define MAX_PEERS 10
#define CACHE_SIZE 512
#define DEFAULT_TTL 10
#define RETRY_INTERVAL_MS 2000

static const uint8_t ESPNOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF,
                                                0xFF, 0xFF, 0xFF};

/* ===================== GLOBAL ===================== */

static uint8_t my_mac[6];
static QueueHandle_t rx_queue;
static QueueHandle_t tx_queue;
static uint32_t local_msg_counter = 1;

/* ===================== PEERS ===================== */

typedef struct {
  uint8_t mac[6];
  bool active;
} peer_t;

static peer_t peers[MAX_PEERS];

/* ===================== ACK TABLE ===================== */

typedef struct {
  uint8_t mac[6];
  bool confirmed;
} ack_entry_t;

static ack_entry_t ack_table[MAX_PEERS];
static uint32_t current_msg_waiting = 0;

/* ===================== UTILS ===================== */

static bool mac_equal(const uint8_t *a, const uint8_t *b) {
  return memcmp(a, b, 6) == 0;
}

static const char *msg_type_to_str(uint8_t type) {
  switch (type) {
  case MSG_TYPE_DISCOVERY:
    return "DISCOVERY";
  case MSG_TYPE_DATA:
    return "DATA";
  case MSG_TYPE_ACK:
    return "ACK";
  case MSG_TYPE_INFO:
    return "INFO";
  default:
    return "UNKNOWN";
  }
}

static void mac_to_str_buf(const uint8_t *mac, char *out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
}

static const char *mac_to_str(const uint8_t *mac) {
  static char buf[4][18];
  static uint8_t idx = 0;
  idx = (idx + 1) % 4;
  mac_to_str_buf(mac, buf[idx]);
  return buf[idx];
}

static void blink_led(int n, int d) {
  while (n--) {
    gpio_set_level(LED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(d));
    gpio_set_level(LED_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(d));
  }
}

static void log_msg(const char *prefix, const espnow_msg_t *m) {
  ESP_LOGI(TAG,
           "%s type=%s id=%u ttl=%u rssi=%d "
           "src=%02X:%02X:%02X:%02X:%02X:%02X "
           "dest=%02X:%02X:%02X:%02X:%02X:%02X "
           "origin=%02X:%02X:%02X:%02X:%02X:%02X",
           prefix, msg_type_to_str(m->type), m->msg_id, m->ttl, m->rssi,
           m->src_mac[0], m->src_mac[1], m->src_mac[2], m->src_mac[3],
           m->src_mac[4], m->src_mac[5], m->dest_mac[0], m->dest_mac[1],
           m->dest_mac[2], m->dest_mac[3], m->dest_mac[4], m->dest_mac[5],
           m->origin_mac[0], m->origin_mac[1], m->origin_mac[2],
           m->origin_mac[3], m->origin_mac[4], m->origin_mac[5]);
}

/* ===================== PEERS ===================== */

static void add_peer(const uint8_t *mac) {
  for (int i = 0; i < MAX_PEERS; i++) {
    if (peers[i].active && mac_equal(peers[i].mac, mac))
      return;
  }

  for (int i = 0; i < MAX_PEERS; i++) {
    if (!peers[i].active) {
      peers[i].active = true;
      memcpy(peers[i].mac, mac, 6);

      esp_now_peer_info_t p = {0};
      memcpy(p.peer_addr, mac, 6);
      p.ifidx = WIFI_IF_STA;
      p.encrypt = false;

      if (esp_now_add_peer(&p) == ESP_OK) {
        ESP_LOGI(TAG, "PEER ADDED %s", mac_to_str(mac));
        blink_led(2, 60);
      } else {
        ESP_LOGE(TAG, "PEER FAILED %s", mac_to_str(mac));
      }

      // add to ack_table if empty
      for (int j = 0; j < MAX_PEERS; j++) {
        if (!ack_table[j].confirmed &&
            mac_equal(ack_table[j].mac, (uint8_t[6]){0})) {
          memcpy(ack_table[j].mac, mac, 6);
          ack_table[j].confirmed = false;
          break;
        }
      }
      break;
    }
  }
}

/* ===================== RX CALLBACK ===================== */

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data,
                           int len) {
  // if (len != sizeof(espnow_msg_t))
  //   return;

  espnow_msg_t msg;
  memcpy(&msg, data, sizeof(msg));
  memcpy(msg.src_mac, info->src_addr, 6);
  memcpy(msg.dest_mac, info->des_addr, 6);
  msg.rssi = info->rx_ctrl->rssi;

  xQueueSendFromISR(rx_queue, &msg, NULL);
}

/* ===================== RX TASK ===================== */

static void espnow_rx_task(void *arg) {
  espnow_msg_t msg;

  while (1) {
    if (!xQueueReceive(rx_queue, &msg, portMAX_DELAY))
      continue;

    if (msg.type != MSG_TYPE_DISCOVERY) {
      log_msg("RX", &msg);
    }

    if (msg.type == MSG_TYPE_DISCOVERY) {
      add_peer(msg.src_mac);
    }

    if (msg.type == MSG_TYPE_DATA && !mac_equal(msg.origin_mac, my_mac)) {
      // send ACK back to root
      espnow_msg_t ack = {0};
      memcpy(ack.origin_mac, msg.origin_mac, 6);
      memcpy(ack.src_mac, my_mac, 6);
      ack.msg_id = msg.msg_id;
      ack.type = MSG_TYPE_ACK;
      memcpy(ack.dest_mac, msg.origin_mac, 6);
      xQueueSend(tx_queue, &ack, 0);

      send_to_serial(&msg);
      blink_led(1, 40);
    }

    if (msg.type == MSG_TYPE_ACK && msg.msg_id == current_msg_waiting) {
      for (int i = 0; i < MAX_PEERS; i++) {
        if (mac_equal(ack_table[i].mac, msg.src_mac)) {
          ack_table[i].confirmed = true;
          ESP_LOGI(TAG, "CONFIRMED msg_id=%lu by %s", msg.msg_id,
                   mac_to_str(msg.src_mac));
        }
      }
    }

    if (msg.type == MSG_TYPE_INFO) {

      ESP_LOGI(TAG, "INFO RECEIVED from %s → %d peers", mac_to_str(msg.src_mac),
               msg.peer_count);

      for (int i = 0; i < msg.peer_count; i++) {
        ESP_LOGI(TAG, "  PEER %d → %02X:%02X:%02X:%02X:%02X:%02X", i,
                 msg.peer_macs[i][0], msg.peer_macs[i][1], msg.peer_macs[i][2],
                 msg.peer_macs[i][3], msg.peer_macs[i][4], msg.peer_macs[i][5]);
      }

      // Send to serial (so PC can see full mesh topology)
      send_to_serial(&msg);
    }
  }
}

/* ===================== TX TASK ===================== */

static esp_err_t espnow_send_checked(const uint8_t *dest_mac,
                                     espnow_msg_t *msg) {
  esp_err_t err = esp_now_send(dest_mac, (uint8_t *)msg, sizeof(espnow_msg_t));
  if (err == ESP_OK) {
    memcpy(msg->dest_mac, dest_mac, 6);
    if (msg->type == MSG_TYPE_DATA || msg->type == MSG_TYPE_INFO)
      send_to_serial(msg);
  } else {
    ESP_LOGE(TAG, "TX FAIL (%s) type=%s id=%lu → %s", esp_err_to_name(err),
             msg_type_to_str(msg->type), msg->msg_id, mac_to_str(dest_mac));
  }
  return err;
}

static void espnow_tx_task(void *arg) {
  espnow_msg_t msg;

  while (1) {
    if (xQueueReceive(tx_queue, &msg, portMAX_DELAY)) {
      switch (msg.type) {
      case MSG_TYPE_DATA:
        for (int i = 0; i < MAX_PEERS; i++) {
          if (peers[i].active)
            espnow_send_checked(peers[i].mac, &msg);
        }
        break;
      case MSG_TYPE_ACK:
        espnow_send_checked(msg.dest_mac, &msg);
        break;
      case MSG_TYPE_DISCOVERY:
        espnow_send_checked(ESPNOW_BROADCAST_MAC, &msg);
        break;
      default:
        break;
      }
    }
  }
}

/* ===================== DISCOVERY TASK ===================== */

static void discovery_task(void *arg) {
  while (1) {
    espnow_msg_t msg = {0};
    memcpy(msg.origin_mac, my_mac, 6);
    memcpy(msg.src_mac, my_mac, 6);
    memcpy(msg.dest_mac, ESPNOW_BROADCAST_MAC, 6);
    msg.msg_id = local_msg_counter++;
    msg.ttl = DEFAULT_TTL;
    msg.type = MSG_TYPE_DISCOVERY;
    memcpy(msg.data, "root_node", 10);

    xQueueSend(tx_queue, &msg, 0);
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

/* ===================== BUTTON TASK ===================== */

static void button_task(void *arg) {
  int last = 1;
  while (1) {
    int cur = gpio_get_level(BUTTON_GPIO);
    if (last == 1 && cur == 0) {
      espnow_msg_t msg = {0};
      memcpy(msg.origin_mac, my_mac, 6);
      memcpy(msg.src_mac, my_mac, 6);
      msg.msg_id = local_msg_counter++;
      msg.ttl = DEFAULT_TTL;
      msg.type = MSG_TYPE_DATA;
      current_msg_waiting = msg.msg_id;

      // reset ACK table
      for (int i = 0; i < MAX_PEERS; i++) {
        if (peers[i].active) {
          memcpy(ack_table[i].mac, peers[i].mac, 6);
          ack_table[i].confirmed = false;
        } else {
          memset(ack_table[i].mac, 0, 6);
          ack_table[i].confirmed = false;
        }
      }

      ESP_LOGI(TAG, "BUTTON → DATA id=%lu", msg.msg_id);
      xQueueSend(tx_queue, &msg, 0);
      blink_led(2, 80);
    }
    last = cur;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/* ===================== RELIABILITY TASK ===================== */

static void reliability_task(void *arg) {
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(RETRY_INTERVAL_MS));

    if (current_msg_waiting == 0)
      continue;

    bool all_confirmed = true;
    char confirmed_macs[128] = {0};

    for (int i = 0; i < MAX_PEERS; i++) {
      if (peers[i].active) {
        if (ack_table[i].confirmed) {
          char buf[18];
          mac_to_str_buf(ack_table[i].mac, buf);
          strcat(confirmed_macs, buf);
          strcat(confirmed_macs, " ");
        } else {
          all_confirmed = false;
        }
      }
    }

    if (!all_confirmed) {
      ESP_LOGW(TAG, "RETRYING msg_id=%lu, pending ACKs from peers",
               current_msg_waiting);

      espnow_msg_t retry = {0};
      memcpy(retry.origin_mac, my_mac, 6);
      memcpy(retry.src_mac, my_mac, 6);
      retry.msg_id = current_msg_waiting;
      retry.ttl = DEFAULT_TTL;
      retry.type = MSG_TYPE_DATA;

      for (int i = 0; i < MAX_PEERS; i++) {
        if (peers[i].active)
          espnow_send_checked(peers[i].mac, &retry);
      }
    } else {
      ESP_LOGI(TAG, "ALL DEVICES CONFIRMED msg_id=%lu by: %s",
               current_msg_waiting, confirmed_macs);
      current_msg_waiting = 0;
    }
  }
}

/* ===================== INIT ===================== */

static void wifi_init(void) {
  esp_netif_init();
  esp_event_loop_create_default();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
}

static void espnow_init(void) {
  esp_now_init();
  esp_now_register_recv_cb(espnow_recv_cb);

  esp_now_peer_info_t b = {0};
  memcpy(b.peer_addr, ESPNOW_BROADCAST_MAC, 6);
  b.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&b);
}

static void gpio_init_all(void) {
  gpio_config_t led = {.pin_bit_mask = 1ULL << LED_GPIO,
                       .mode = GPIO_MODE_OUTPUT};
  gpio_config(&led);

  gpio_config_t btn = {.pin_bit_mask = 1ULL << BUTTON_GPIO,
                       .mode = GPIO_MODE_INPUT,
                       .pull_up_en = GPIO_PULLUP_ENABLE};
  gpio_config(&btn);
}

static void info_task(void *arg) {
  while (1) {

    espnow_msg_t msg = {0};

    memcpy(msg.origin_mac, my_mac, 6);
    memcpy(msg.src_mac, my_mac, 6);

    msg.msg_id = local_msg_counter++;
    msg.ttl = DEFAULT_TTL;
    msg.type = MSG_TYPE_INFO;

    uint8_t count = 0;

    for (int i = 0; i < MAX_PEERS; i++) {
      if (!peers[i].active)
        continue;

      memcpy(msg.peer_macs[count], peers[i].mac, 6);
      count++;
    }

    msg.peer_count = count;

    ESP_LOGI(TAG, "ROOT → sending %d peers", count);

    xQueueSend(tx_queue, &msg, 0);

    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}

/* ===================== MAIN ===================== */

void run_now(void) {
  nvs_flash_init();

  rx_queue = xQueueCreate(32, sizeof(espnow_msg_t));
  tx_queue = xQueueCreate(32, sizeof(espnow_msg_t));

  gpio_init_all();
  wifi_init();
  espnow_init();
  esp_wifi_get_mac(WIFI_IF_STA, my_mac);

  xTaskCreate(espnow_rx_task, "rx", 4096, NULL, 5, NULL);
  xTaskCreate(espnow_tx_task, "tx", 4096, NULL, 5, NULL);
  xTaskCreate(button_task, "button", 2048, NULL, 4, NULL);
  xTaskCreate(discovery_task, "discovery", 2048, NULL, 3, NULL);
  xTaskCreate(reliability_task, "reliability", 4096, NULL, 4, NULL);
  xTaskCreate(info_task, "info", 4096, NULL, 3, NULL);

  ESP_LOGI(TAG, "ESP-NOW ROOT NODE READY");
}