#include "logger.h"

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

  case MSG_TYPE_FWD:
    return "MSG_TYPE_FWD";

  case MSG_TYPE_PING:
    return "MSG_TYPE_PING";

  case MSG_TYPE_PONG:
    return "MSG_TYPE_PONG";

  default:
    return "MSG_UNKNOWN";
  }
}

bool mac_equal(const uint8_t *a, const uint8_t *b) {
  return memcmp(a, b, 6) == 0;
}
