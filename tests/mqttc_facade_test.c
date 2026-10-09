#include <cpkt/mqttc.h>

#include <string.h>

int main(void) {
  cpkt_mqtt_u8 subscribe_buffer[64];
  cpkt_mqtt_u8 unsubscribe_buffer[64];
  cpkt_mqtt_u8 fixed_header_buffer[4];
  struct cpkt_mqtt_fixed_header fixed_header;
  cpkt_mqtt_ssize result;

  fixed_header.control_type = CPKT_MQTT_CONTROL_PINGREQ;
  fixed_header.control_flags = 0U;
  fixed_header.remaining_length = 0U;
  result = cpkt_mqtt_pack_fixed_header(
      fixed_header_buffer, sizeof(fixed_header_buffer), &fixed_header);
  if (result != 2 || fixed_header_buffer[0] != 0xc0U ||
      fixed_header_buffer[1] != 0U)
    return 1;
  result = cpkt_mqtt_pack_subscribe_request(subscribe_buffer,
                                            sizeof(subscribe_buffer), 7U, "one",
                                            1U, "two", 2U, NULL);
  if (result <= 0 || subscribe_buffer[0] != 0x82U)
    return 2;
  result = cpkt_mqtt_pack_unsubscribe_request(
      unsubscribe_buffer, sizeof(unsubscribe_buffer), 8U, "one", "two", NULL);
  if (result <= 0 || unsubscribe_buffer[0] != 0xa2U)
    return 3;
  if (strcmp(cpkt_mqtt_error_str(CPKT_MQTT_ERROR_NULLPTR),
             "MQTT_ERROR_NULLPTR") != 0)
    return 4;
  return 0;
}
