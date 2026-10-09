#include <mqtt.h>

int main(void) {
  const char *message;

  message = mqtt_error_str(MQTT_ERROR_NULLPTR);
  return message == 0 || message[0] == '\0';
}
