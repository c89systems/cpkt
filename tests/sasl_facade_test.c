#include <cpkt/sasl.h>

#include <string.h>

static int facade_secret(cpkt_sasl *connection, void *context, int id,
                         const cpkt_sasl_secret **secret_out) {
  static const unsigned char password[] = "secret";
  static const cpkt_sasl_secret secret = {password, 6};
  (void)connection;
  (void)context;
  (void)id;
  *secret_out = &secret;
  return CPKT_SASL_OK;
}

static int facade_global_simple(void *context, int id, const char **result,
                                unsigned long *result_byte_count) {
  int *calls;
  (void)id;
  calls = (int *)context;
  ++*calls;
  *result = "facade-user";
  if (result_byte_count != 0)
    *result_byte_count = 11;
  return CPKT_SASL_OK;
}

static int start_external_client(void) {
  cpkt_sasl *client;
  cpkt_sasl_interaction *interactions;
  const char *output;
  const char *mechanism;
  unsigned long output_length;
  int status;
  status = CPKT_SASL_FAIL;
  client = cpkt_sasl_client_new("test", "localhost", 0, 0, 0, 0, &status);
  if (client == 0 || status != CPKT_SASL_OK)
    return 0;
  interactions = 0;
  output = 0;
  mechanism = 0;
  output_length = 0;
  status = client->set_external_authentication(client, "facade-user");
  if (status == CPKT_SASL_OK)
    status = client->start(client, "EXTERNAL", &interactions, &output,
                           &output_length, &mechanism);
  client->close(client);
  return status == CPKT_SASL_OK;
}

int main(void) {
  const char *implementation;
  const char *version;
  const char *message;
  const char *mechanisms;
  cpkt_sasl *client;
  cpkt_sasl_security_properties properties;
  cpkt_sasl_callbacks callbacks;
  unsigned long mechanisms_length;
  int mechanisms_count;
  int major;
  int minor;
  int step;
  int patch;
  int status;
  int global_calls;

  implementation = 0;
  version = 0;
  major = 0;
  minor = 0;
  step = 0;
  patch = 0;
  cpkt_sasl_version(&implementation, &version, &major, &minor, &step, &patch);
  if (implementation == 0 || version == 0 || major < 2)
    return 1;
  message = cpkt_sasl_error_string(CPKT_SASL_BADPARAM, 0, 0);
  if (message == 0 || strlen(message) == 0U)
    return 2;
  if (cpkt_sasl_set_path(CPKT_SASL_PATH_PLUGIN,
                         "/cpkt-no-external-sasl-plugins") != CPKT_SASL_OK)
    return 10;
  if (cpkt_sasl_client_initialize(0) != CPKT_SASL_OK)
    return 3;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.secret = facade_secret;
  status = CPKT_SASL_FAIL;
  client = cpkt_sasl_client_new("imap", "mail.example.test", 0, 0, &callbacks,
                                0, &status);
  if (client == 0 || status != CPKT_SASL_OK)
    return 4;
  mechanisms = 0;
  mechanisms_length = 0;
  mechanisms_count = 0;
  if (client->list_mechanisms(client, 0, 0, " ", 0, &mechanisms,
                              &mechanisms_length,
                              &mechanisms_count) != CPKT_SASL_OK ||
      mechanisms == 0 || strstr(mechanisms, "GSSAPI") == 0 ||
      mechanisms_count < 1 || mechanisms_length == 0) {
    client->close(client);
    return 11;
  }
  if (client->server_start(client, "PLAIN", 0, 0, 0, 0) != CPKT_SASL_BADPARAM) {
    client->close(client);
    return 5;
  }
  memset(&properties, 0, sizeof(properties));
  properties.minimum_ssf = 0;
  properties.maximum_ssf = 256;
  if (client->set_security_properties(client, &properties) != CPKT_SASL_OK) {
    client->close(client);
    return 6;
  }
  if (client->set_external_ssf(client, 0) != CPKT_SASL_OK) {
    client->close(client);
    return 7;
  }
  if (client->error_detail(client) == 0) {
    client->close(client);
    return 8;
  }
  client->close(client);
  if (cpkt_sasl_client_finish() != CPKT_SASL_OK)
    return 9;
  memset(&callbacks, 0, sizeof(callbacks));
  global_calls = 0;
  callbacks.context = &global_calls;
  callbacks.simple = facade_global_simple;
  if (cpkt_sasl_client_initialize(&callbacks) != CPKT_SASL_OK ||
      !start_external_client() || global_calls == 0 ||
      cpkt_sasl_client_initialize(0) != CPKT_SASL_OK)
    return 12;
  global_calls = 0;
  if (!start_external_client() || global_calls == 0 ||
      cpkt_sasl_client_finish() != CPKT_SASL_CONTINUE)
    return 13;
  global_calls = 0;
  if (!start_external_client() || global_calls == 0 ||
      cpkt_sasl_client_finish() != CPKT_SASL_OK)
    return 14;
  return 0;
}
