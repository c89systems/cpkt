#include <cpkt/sasl_plugin.h>
#include <string.h>

int main(void) {
  cpkt_sasl *connection;
  cpkt_sasl_md5_context digest_context;
  cpkt_sasl_security_properties security;
  cpkt_sasl_http_request request;
  const cpkt_sasl_callbacks *selected_callbacks;
  const void *payload;
  const char *external_identity;
  const char *mechanisms;
  unsigned long mechanisms_length;
  int mechanisms_count;
  int status;

  if (cpkt_sasl_error_string(CPKT_SASL_BADPARAM, 0, 0) == 0) {
    return 1;
  }
  cpkt_sasl_md5_initialize(&digest_context);
  if (cpkt_sasl_client_add_plugin(0, 0, 0) != CPKT_SASL_BADPARAM ||
      cpkt_sasl_server_add_plugin(0, 0, 0) != CPKT_SASL_BADPARAM ||
      cpkt_sasl_canonicalizer_add_plugin(0, 0, 0) != CPKT_SASL_BADPARAM ||
      cpkt_sasl_auxiliary_add_plugin(0, 0, 0) != CPKT_SASL_BADPARAM) {
    return 6;
  }
  if (cpkt_sasl_client_initialize(0) != CPKT_SASL_OK) {
    return 2;
  }
  status = CPKT_SASL_FAIL;
  connection =
      cpkt_sasl_client_new("cpkt-package", "localhost", 0, 0, 0, 0, &status);
  if (connection == 0 || status != CPKT_SASL_OK || connection->close == 0 ||
      connection->set_security_properties == 0 ||
      connection->get_security_properties == 0 ||
      connection->get_http_request == 0 ||
      connection->get_callback_record == 0 ||
      connection->get_option_context == 0 ||
      connection->get_delegated_payload == 0 ||
      connection->set_text_property == 0 || connection->encode_vector == 0 ||
      connection->auxiliary_context == 0) {
    cpkt_sasl_client_finish();
    return 3;
  }
  selected_callbacks = 0;
  payload = 0;
  external_identity = 0;
  memset(&security, 0, sizeof(security));
  security.maximum_buffer_bytes = 1024;
  memset(&request, 0, sizeof(request));
  request.method = "GET";
  request.uri = "/";
  if (connection->set_security_properties(connection, &security) !=
          CPKT_SASL_OK ||
      connection->get_security_properties(connection, &security) !=
          CPKT_SASL_OK ||
      security.maximum_buffer_bytes != 1024 ||
      connection->set_http_request(connection, &request) != CPKT_SASL_OK ||
      connection->get_http_request(connection, &request) != CPKT_SASL_OK ||
      request.method == 0 || strcmp(request.method, "GET") != 0 ||
      connection->set_text_property(connection,
                                    CPKT_SASL_PROPERTY_EXTERNAL_AUTHENTICATION,
                                    "sdk") != CPKT_SASL_OK ||
      connection->get_text_property(connection,
                                    CPKT_SASL_PROPERTY_EXTERNAL_AUTHENTICATION,
                                    &external_identity) != CPKT_SASL_OK ||
      external_identity == 0 || strcmp(external_identity, "sdk") != 0 ||
      connection->get_callback_record(connection, &selected_callbacks) !=
          CPKT_SASL_OK ||
      selected_callbacks != 0 ||
      connection->get_delegated_payload(connection, &payload) !=
          CPKT_SASL_NOTDONE ||
      payload != 0) {
    connection->close(connection);
    cpkt_sasl_client_finish();
    return 7;
  }
  mechanisms = 0;
  mechanisms_length = 0;
  mechanisms_count = 0;
  if (connection->list_mechanisms(connection, 0, 0, " ", 0, &mechanisms,
                                  &mechanisms_length,
                                  &mechanisms_count) != CPKT_SASL_OK ||
      mechanisms == 0 || strstr(mechanisms, "GSSAPI") == 0 ||
      mechanisms_length == 0 || mechanisms_count < 1) {
    connection->close(connection);
    cpkt_sasl_client_finish();
    return 5;
  }
  connection->close(connection);
  return cpkt_sasl_client_finish() == CPKT_SASL_OK ? 0 : 4;
}
