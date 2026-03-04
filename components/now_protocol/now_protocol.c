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

/* ===================== DEDUP ===================== */

typedef struct {
  uint8_t src_mac[6];
  uint8_t dest_mac[6];
  uint8_t origin_mac[6];
  uint32_t msg_id;
  uint8_t type;
  char data[10];
} cache_entry_t;

static cache_entry_t cache[CACHE_SIZE];
static uint8_t cache_idx = 0;

/* ===================== UTILS ===================== */

const char *msg_type_to_str(uint8_t type) {
  switch ((msg_type_t)type) {
  case MSG_TYPE_DISCOVERY:
    return "MSG_TYPE_DISCOVERY";

  case MSG_TYPE_DATA:
    return "MSG_TYPE_DATA";

  case MSG_TYPE_ACK:
    return "MSG_TYPE_ACK";

  case MSG_TYPE_INFO:
    return "MSG_TYPE_INFO";

  default:
    return "MSG_UNKNOWN";
  }
}

static bool mac_equal(const uint8_t *a, const uint8_t *b) {
  return memcmp(a, b, 6) == 0;
}

static inline void mac_to_str_buf(const uint8_t *mac, char *out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
}

static inline const char *mac_to_str(const uint8_t *mac) {
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
           m->src_mac[4], m->src_mac[5],

           m->dest_mac[0], m->dest_mac[1], m->dest_mac[2], m->dest_mac[3],
           m->dest_mac[4], m->dest_mac[5],

           m->origin_mac[0], m->origin_mac[1], m->origin_mac[2],
           m->origin_mac[3], m->origin_mac[4], m->origin_mac[5]);
}

/* ===================== DEDUP ===================== */

static bool msg_seen(const espnow_msg_t *m) {
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (cache[i].type == m->type && cache[i].msg_id == m->msg_id &&
        mac_equal(cache[i].origin_mac, m->origin_mac)) {
      return true;
    }
  }

  memcpy(cache[cache_idx].origin_mac, m->origin_mac, 6);
  cache[cache_idx].msg_id = m->msg_id;
  cache[cache_idx].type = m->type;
  cache_idx = (cache_idx + 1) % CACHE_SIZE;

  return false;
}

/* ===================== PEER ===================== */

static void add_peer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac))
    return;

  for (int i = 0; i < MAX_PEERS; i++) {
    if (!peers[i].active) {
      peers[i].active = true;
      memcpy(peers[i].mac, mac, 6);

      esp_now_peer_info_t p = {0};
      memcpy(p.peer_addr, mac, 6);
      p.ifidx = WIFI_IF_STA;
      p.encrypt = false;

      if (esp_now_add_peer(&p) == ESP_OK) {
        ESP_LOGI(TAG, "PEER ADDED %02X:%02X:%02X:%02X:%02X:%02X", mac[0],
                 mac[1], mac[2], mac[3], mac[4], mac[5]);
        blink_led(2, 60);
      } else {
        ESP_LOGE(TAG, "PEER FAILED %02X:%02X:%02X:%02X:%02X:%02X", mac[0],
                 mac[1], mac[2], mac[3], mac[4], mac[5]);
      }
      return;
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

    if (msg.type == MSG_TYPE_DATA && !mac_equal(msg.src_mac, my_mac) &&
        mac_equal(msg.origin_mac, my_mac)) {

      ESP_LOGI(TAG, "IGNORED SELF msg_id=%lu type=%s", msg.msg_id,
               msg_type_to_str(msg.type));
      continue;
    }

    if (msg.type == MSG_TYPE_DISCOVERY) {
      add_peer(msg.src_mac);
    } else {
      log_msg("RX", &msg);
    }

    if (msg.type != MSG_TYPE_ACK) {
      if (msg_seen(&msg))
        continue;
    }

    if (msg.type == MSG_TYPE_DATA || msg.type == MSG_TYPE_INFO) {
      send_to_serial(&msg);
    }

    switch (msg.type) {
    case MSG_TYPE_ACK: {
      ESP_LOGI(TAG, "CONFIRMED msg_id=%lu by %02X:%02X:%02X:%02X:%02X:%02X",
               msg.msg_id, msg.src_mac[0], msg.src_mac[1], msg.src_mac[2],
               msg.src_mac[3], msg.src_mac[4], msg.src_mac[5]);
      break;
    }

    case MSG_TYPE_DATA: {
      blink_led(1, 40);

      // Send ACK back to sender
      espnow_msg_t ack = {0};
      memcpy(ack.origin_mac, msg.origin_mac, 6);
      memcpy(ack.src_mac, my_mac, 6);
      memcpy(ack.data, msg.data, sizeof(msg.data));
      ack.msg_id = msg.msg_id;
      ack.type = MSG_TYPE_ACK;
      xQueueSend(tx_queue, &ack, 0);

      // Dedup: if already seen, do not forward
      if (msg_seen(&msg))
        break;

      // Forward message to other peers if TTL > 0
      if (msg.ttl > 0) {
        for (int i = 0; i < MAX_PEERS; i++) {
          if (!peers[i].active)
            continue;

          // Skip immediate sender and self
          if (mac_equal(peers[i].mac, msg.src_mac) ||
              mac_equal(peers[i].mac, my_mac))
            continue;

          espnow_msg_t fwd = msg; // copy original message
          fwd.ttl--;              // decrease TTL
          fwd.forwarded = true;   // mark as forwarded
          memcpy(fwd.src_mac, my_mac, 6);
          memcpy(fwd.dest_mac, peers[i].mac, 6);

          xQueueSend(tx_queue, &fwd, 0);
          log_msg("FWD", &fwd);
        }
      }

      // Deliver to serial / application layer
      send_to_serial(&msg);
      break;
    }

    case MSG_TYPE_INFO: {
      log_msg("RX", &msg);
      send_to_serial(&msg);
      ESP_LOGI(TAG, "INFO RECEIVED → %d peers", msg.peer_count);

      for (int i = 0; i < msg.peer_count; i++) {
        ESP_LOGI(TAG, "PEER %d → %02X:%02X:%02X:%02X:%02X:%02X", i,
                 msg.peer_macs[i][0], msg.peer_macs[i][1], msg.peer_macs[i][2],
                 msg.peer_macs[i][3], msg.peer_macs[i][4], msg.peer_macs[i][5]);
      }

      break;
    }

    default:
      break;
    }
  }
}

static esp_err_t espnow_send_checked(const uint8_t *dest_mac,
                                     espnow_msg_t *msg) {
  esp_err_t err = esp_now_send(dest_mac, (uint8_t *)msg, sizeof(espnow_msg_t));

  if (err == ESP_OK) {

    if (msg->type != MSG_TYPE_DISCOVERY) {
      memcpy(msg->dest_mac, dest_mac, 6);
      log_msg("TX", msg);
    }

    if (msg->type == MSG_TYPE_DATA || msg->type == MSG_TYPE_INFO) {
      send_to_serial(msg);
    }

  } else {
    ESP_LOGE(TAG, "TX FAIL (%s) type=%s id=%lu → %s", esp_err_to_name(err),
             msg_type_to_str(msg->type), msg->msg_id, mac_to_str(dest_mac));
  }

  return err;
}

/* ===================== TX TASK ===================== */

static void espnow_tx_task(void *arg) {
  espnow_msg_t msg;

  while (1) {
    if (xQueueReceive(tx_queue, &msg, portMAX_DELAY)) {

      switch (msg.type) {

      case MSG_TYPE_DATA:
        if (msg.forwarded) {
          // Forwarded messages → send only to dest_mac
          espnow_send_checked(msg.dest_mac, &msg);
        } else {
          // Local message → send to all connected peers
          for (int i = 0; i < MAX_PEERS; i++) {
            if (!peers[i].active)
              continue;
            espnow_send_checked(peers[i].mac, &msg);
          }
        }
        break;

      case MSG_TYPE_ACK:
        // esp_now_send(msg.origin_mac, (uint8_t *)&msg, sizeof(msg));
        espnow_send_checked(msg.origin_mac, &msg);
        break;
      case MSG_TYPE_DISCOVERY:
        // esp_now_send(ESPNOW_BROADCAST_MAC, (uint8_t *)&msg, sizeof(msg));
        espnow_send_checked(ESPNOW_BROADCAST_MAC, &msg);
        break;

      case MSG_TYPE_INFO:
        if (msg.forwarded) {
          espnow_send_checked(msg.dest_mac, &msg);
        } else {
          for (int i = 0; i < MAX_PEERS; i++) {
            if (!peers[i].active)
              continue;

            espnow_send_checked(peers[i].mac, &msg);
          }
        }
        break;

      default:
        ESP_LOGI(TAG, "Unidentified TYPE: %u", msg.type);
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
    memcpy(msg.data, "teste", 10);

    xQueueSend(tx_queue, &msg, 0);

    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

/* ===================== BUTTON TASK ===================== */

static void button_task(void *arg) {
  int last = 1;

  while (1) {
    int cur = gpio_get_level(BUTTON_GPIO);

    if (last == 1 && cur == 0) { // button pressed
      espnow_msg_t msg = {0};
      memcpy(msg.origin_mac, my_mac, 6);
      memcpy(msg.src_mac, my_mac, 6);
      msg.msg_id = local_msg_counter++;
      msg.ttl = DEFAULT_TTL;
      msg.type = MSG_TYPE_DATA;
      msg.forwarded = false;

      ESP_LOGI(TAG, "BUTTON → DATA id=%lu", msg.msg_id);

      xQueueSend(tx_queue, &msg, 0);
      blink_led(2, 80);
    }

    last = cur;
    vTaskDelay(pdMS_TO_TICKS(50));
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
    msg.forwarded = false;

    uint8_t count = 0;

    for (int i = 0; i < MAX_PEERS; i++) {
      if (!peers[i].active)
        continue;

      memcpy(msg.peer_macs[count], peers[i].mac, 6);
      count++;

      if (count >= 10)
        break;
    }

    msg.peer_count = count;

    ESP_LOGI(TAG, "INFO → sending %d peers", count);

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
  xTaskCreate(info_task, "info", 4096, NULL, 3, NULL);

  ESP_LOGI(TAG, "ESP-NOW MESH READY");
}
