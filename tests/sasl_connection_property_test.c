#include <cpkt/sasl.h>

#include <string.h>

static int option_calls;

static int selected_option(void *context, const char *plugin, const char *name,
                           const char **result, unsigned long *length) {
  (void)plugin;
  if (context != &option_calls || name == 0 || result == 0)
    return CPKT_SASL_BADPARAM;
  if (strcmp(name, "probe") != 0)
    return CPKT_SASL_FAIL;
  ++option_calls;
  *result = "enabled";
  if (length != 0)
    *length = 7;
  return CPKT_SASL_OK;
}

int main(void) {
  cpkt_sasl_callbacks callbacks;
  const cpkt_sasl_callbacks *selected = 0;
  cpkt_sasl_security_properties security, returned_security;
  cpkt_sasl_http_request request, returned_request;
  cpkt_sasl *client, *server;
  const char *text = 0;
  const char *names[] = {"n", 0};
  const char *values[] = {"v", 0};
  unsigned char entity[] = {1, 2, 3};
  unsigned long number = 0;
  void *context = 0;
  int status = 0;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.context = &option_calls;
  callbacks.option = selected_option;
  if (cpkt_sasl_client_initialize(0) != CPKT_SASL_OK ||
      cpkt_sasl_server_initialize(0, "property-test") != CPKT_SASL_OK)
    return 1;
  client =
      cpkt_sasl_client_new("test", "localhost", 0, 0, &callbacks, 0, &status);
  server = cpkt_sasl_server_new("test", "localhost", 0, 0, 0, 0, 0, &status);
  if (client == 0 || server == 0 || status != CPKT_SASL_OK)
    return 2;
  if (client->get_callback_record(client, &selected) != CPKT_SASL_OK ||
      selected == 0 || selected == &callbacks ||
      selected->context != &option_calls ||
      selected->option != selected_option ||
      client->get_option_context(client, &context) != CPKT_SASL_OK ||
      context != &option_calls ||
      selected->option(selected->context, 0, "probe", &text, 0) !=
          CPKT_SASL_OK ||
      option_calls != 1 || strcmp(text, "enabled") != 0)
    return 3;
  memset(&security, 0, sizeof(security));
  security.minimum_ssf = 1;
  security.maximum_ssf = 5;
  security.maximum_buffer_bytes = 4096;
  security.security_flags = CPKT_SASL_SECURITY_NO_ANONYMOUS;
  security.property_names = names;
  security.property_values = values;
  if (client->set_security_properties(client, &security) != CPKT_SASL_OK ||
      client->get_security_properties(client, &returned_security) !=
          CPKT_SASL_OK ||
      returned_security.minimum_ssf != 1 ||
      returned_security.maximum_ssf != 5 ||
      returned_security.maximum_buffer_bytes != 4096 ||
      returned_security.security_flags != CPKT_SASL_SECURITY_NO_ANONYMOUS ||
      returned_security.property_names != names ||
      returned_security.property_values != values)
    return 4;
  security.maximum_buffer_bytes = 0;
  if (client->set_security_properties(client, &security) != CPKT_SASL_TOOWEAK ||
      client->get_security_properties(client, &returned_security) !=
          CPKT_SASL_OK ||
      returned_security.maximum_buffer_bytes != 4096)
    return 5;
  if (client->set_external_ssf(client, 7) != CPKT_SASL_OK ||
      client->get_number_property(client, CPKT_SASL_PROPERTY_EXTERNAL_SSF,
                                  &number) != CPKT_SASL_OK ||
      number != 7 ||
      client->set_text_property(client,
                                CPKT_SASL_PROPERTY_EXTERNAL_AUTHENTICATION,
                                "external-user") != CPKT_SASL_OK ||
      client->get_text_property(client,
                                CPKT_SASL_PROPERTY_EXTERNAL_AUTHENTICATION,
                                &text) != CPKT_SASL_OK ||
      text == 0 || strcmp(text, "external-user") != 0)
    return 6;
  if (client->set_text_property(client,
                                CPKT_SASL_PROPERTY_EXTERNAL_AUTHENTICATION,
                                0) != CPKT_SASL_OK ||
      client->get_text_property(client,
                                CPKT_SASL_PROPERTY_EXTERNAL_AUTHENTICATION,
                                &text) != CPKT_SASL_OK ||
      text != 0)
    return 7;
  memset(&request, 0, sizeof(request));
  request.method = "POST";
  request.uri = "/resource";
  request.entity = entity;
  request.entity_byte_count = sizeof(entity);
  request.non_persistent = 1;
  if (client->set_http_request(client, &request) != CPKT_SASL_OK ||
      client->get_http_request(client, &returned_request) != CPKT_SASL_OK ||
      strcmp(returned_request.method, "POST") != 0 ||
      strcmp(returned_request.uri, "/resource") != 0 ||
      returned_request.entity != entity ||
      returned_request.entity_byte_count != sizeof(entity) ||
      returned_request.non_persistent != 1 ||
      client->set_http_request(client, 0) != CPKT_SASL_OK ||
      client->get_http_request(client, &returned_request) != CPKT_SASL_OK ||
      returned_request.method != 0 || returned_request.entity != 0)
    return 8;
  if (client->set_text_property(client, CPKT_SASL_PROPERTY_LOCAL_ENDPOINT,
                                "127.0.0.1;1234") != CPKT_SASL_OK ||
      client->get_text_property(client, CPKT_SASL_PROPERTY_LOCAL_ENDPOINT,
                                &text) != CPKT_SASL_OK ||
      strcmp(text, "127.0.0.1;1234") != 0 ||
      client->set_text_property(client, CPKT_SASL_PROPERTY_LOCAL_ENDPOINT, 0) !=
          CPKT_SASL_OK ||
      client->get_text_property(client, CPKT_SASL_PROPERTY_LOCAL_ENDPOINT,
                                &text) != CPKT_SASL_NOTDONE ||
      client->set_text_property(client, CPKT_SASL_PROPERTY_REMOTE_ENDPOINT,
                                "invalid;port") != CPKT_SASL_BADPARAM)
    return 9;
  if (client->set_text_property(client, CPKT_SASL_PROPERTY_DEFAULT_REALM,
                                "example") != CPKT_SASL_BADPROT ||
      server->set_text_property(server, CPKT_SASL_PROPERTY_DEFAULT_REALM,
                                "EXAMPLE.TEST") != CPKT_SASL_OK ||
      server->get_text_property(server, CPKT_SASL_PROPERTY_DEFAULT_REALM,
                                &text) != CPKT_SASL_OK ||
      strcmp(text, "EXAMPLE.TEST") != 0 ||
      server->set_text_property(server, CPKT_SASL_PROPERTY_APPLICATION,
                                "updated-app") != CPKT_SASL_OK ||
      server->get_text_property(server, CPKT_SASL_PROPERTY_APPLICATION,
                                &text) != CPKT_SASL_OK ||
      strcmp(text, "updated-app") != 0 ||
      client->set_text_property(client, CPKT_SASL_PROPERTY_MECHANISM, "bad") !=
          CPKT_SASL_BADPARAM)
    return 10;
  client->close(client);
  server->close(server);
  if (cpkt_sasl_client_finish() != CPKT_SASL_OK ||
      cpkt_sasl_server_finish() != CPKT_SASL_OK)
    return 11;
  return 0;
}
