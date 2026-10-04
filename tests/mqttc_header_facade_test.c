#include <cpkt/mqttc.h>

int main(void) {
  struct cpkt_mqtt_fixed_header header;
  cpkt_mqtt_u8 buffer[4];

  header.control_type = CPKT_MQTT_CONTROL_PINGREQ;
  header.control_flags = 0U;
  header.remaining_length = 0U;
  buffer[0] = 0U;
  return (int)(header.remaining_length + buffer[0]);
}
