#include <cpkt/sasl.h>

#include <sasl/sasl.h>

#include <limits.h>
#include <string.h>

static int mock_connection;
static const sasl_callback_t *mock_callbacks;
static const sasl_callback_t *mock_client_global_callbacks;
static const sasl_callback_t *mock_server_global_callbacks;
static int mock_client_initializations;
static int mock_server_initializations;
static int mock_start_calls;
static int mock_step_calls;
static int mock_secret_mode;
static unsigned char mock_password[] = "secret";
static cpkt_sasl_secret mock_public_secret;
static sasl_interact_t mock_start_interactions[2];
static sasl_interact_t mock_step_interactions[301];
static int mock_extended_callback_calls;
static void *mock_delegated_credential = &mock_connection;
static const char *mock_source = "GSSAPI";
static const char *mock_mechanism = "GSSAPI";
static int mock_provider_credential_releases;
static int mock_application_credential_releases;
static unsigned long mock_option_length = 6;
static int mock_option_status = SASL_OK;
static int mock_option_calls;

/* The injected provider test links only the receiver implementation. Native
 * global shutdown owns these plugin registries in the production build. */
void cpkt_sasl_client_plugins_cleanup(void) {}
void cpkt_sasl_server_plugins_cleanup(void) {}
void cpkt_sasl_auxiliary_plugins_cleanup(void) {}
void cpkt_sasl_canonicalizers_cleanup(void) {}
void cpkt_sasl_plugin_utils_forget_connection(sasl_conn_t *native) {
  (void)native;
}
void cpkt_sasl_plugin_utils_cleanup(void) {}
void cpkt_sasl_plugin_utils_update_global_context(int is_server, void *context,
                                                  int select_shared) {
  (void)is_server;
  (void)context;
  (void)select_shared;
}

typedef union mock_callback_bridge {
  int (*generic)(void);
  sasl_getsimple_t *simple;
  sasl_getsecret_t *secret;
  sasl_chalprompt_t *challenge;
  sasl_getopt_t *option;
} mock_callback_bridge;

static int mock_extended_simple(void *context, int identifier,
                                const char **result, unsigned long *length) {
  (void)context;
  if (identifier != SASL_CB_LANGUAGE && identifier != SASL_CB_CNONCE)
    return SASL_BADPARAM;
  ++mock_extended_callback_calls;
  *result = identifier == SASL_CB_LANGUAGE ? "en" : "nonce";
  if (length != 0)
    *length = (unsigned long)strlen(*result);
  return SASL_OK;
}

static int mock_extended_challenge(void *context, int identifier,
                                   const char *challenge, const char *prompt,
                                   const char *default_result,
                                   const char **result, unsigned long *length) {
  (void)context;
  (void)challenge;
  (void)prompt;
  (void)default_result;
  if (identifier != SASL_CB_NOECHOPROMPT)
    return SASL_BADPARAM;
  ++mock_extended_callback_calls;
  *result = "private";
  if (length != 0)
    *length = 7;
  return SASL_OK;
}

static const sasl_callback_t *
mock_find_callback(const sasl_callback_t *callbacks, unsigned long identifier) {
  const sasl_callback_t *callback;
  callback = callbacks;
  while (callback != 0 && callback->id != SASL_CB_LIST_END) {
    if (callback->id == identifier)
      return callback;
    ++callback;
  }
  return 0;
}

static const sasl_callback_t *mock_callback(unsigned long identifier) {
  return mock_find_callback(mock_callbacks, identifier);
}

static int mock_simple(void *context, int identifier, const char **result,
                       unsigned long *result_byte_count) {
  int *call_count;
  call_count = (int *)context;
  if (identifier != SASL_CB_AUTHNAME || result == 0)
    return SASL_BADPARAM;
  *call_count += 1;
  *result = "facade-authentication-user";
  if (result_byte_count != 0)
    *result_byte_count = (unsigned long)strlen(*result);
  return SASL_OK;
}

static int mock_option(void *context, const char *plugin, const char *name,
                       const char **result, unsigned long *length) {
  (void)plugin;
  if (context == 0 || name == 0 || result == 0)
    return SASL_BADPARAM;
  ++mock_option_calls;
  *result = "option";
  if (length != 0)
    *length = mock_option_length;
  return mock_option_status;
}

static int mock_option_boundaries(const sasl_callback_t *callback) {
  mock_callback_bridge bridge;
  unsigned long lengths[7];
  unsigned native_length;
  const char *result;
  int count, index, error, null_length, expected, status, previous;

  if (callback == 0 || callback->proc == 0)
    return 1;
  bridge.generic = callback->proc;
  lengths[0] = 0;
  lengths[1] = 6;
  lengths[2] = (unsigned long)UINT_MAX - 1UL;
  lengths[3] = UINT_MAX;
  count = 4;
#if ULONG_MAX > UINT_MAX
  lengths[count++] = (unsigned long)UINT_MAX + 1UL;
  lengths[count++] = (unsigned long)UINT_MAX + 37UL;
  lengths[count++] = ULONG_MAX;
#endif
  for (index = 0; index < count; ++index)
    for (error = 0; error < 2; ++error)
      for (null_length = 0; null_length < 2; ++null_length) {
        mock_option_length = lengths[index];
        mock_option_status = error ? SASL_FAIL : SASL_OK;
        expected =
            lengths[index] > UINT_MAX ? SASL_BADPARAM : mock_option_status;
        result = 0;
        native_length = 37;
        previous = mock_option_calls;
        status = bridge.option(callback->context, "cpkt-test", "option",
                               &result, null_length ? 0 : &native_length);
        if (status != expected || mock_option_calls != previous + 1 ||
            result == 0 || strcmp(result, "option") != 0 ||
            native_length != (null_length || lengths[index] > UINT_MAX
                                  ? 37U
                                  : (unsigned)lengths[index]))
          return 1;
      }
  mock_option_length = 6;
  mock_option_status = SASL_OK;
  return 0;
}

static int mock_secret(cpkt_sasl *connection, void *context, int identifier,
                       const cpkt_sasl_secret **result) {
  int *call_count;
  (void)connection;
  call_count = (int *)context;
  if (identifier != SASL_CB_PASS || result == 0)
    return CPKT_SASL_BADPARAM;
  ++*call_count;
  if (mock_secret_mode == 1) {
    *result = 0;
  } else {
    mock_public_secret.data = mock_secret_mode == 2 ? 0 : mock_password;
    mock_public_secret.byte_count = mock_secret_mode == 3 ? 0 : 6;
    *result = &mock_public_secret;
  }
  return CPKT_SASL_OK;
}

int sasl_client_init(const sasl_callback_t *callbacks) {
  if (mock_client_initializations++ == 0)
    mock_client_global_callbacks = callbacks;
  return SASL_OK;
}

int sasl_server_init(const sasl_callback_t *callbacks,
                     const char *application_name) {
  (void)application_name;
  if (mock_server_initializations++ == 0)
    mock_server_global_callbacks = callbacks;
  return SASL_OK;
}

int sasl_client_done(void) {
  if (mock_client_initializations == 0)
    return SASL_NOTINIT;
  if (--mock_client_initializations != 0)
    return SASL_CONTINUE;
  mock_client_global_callbacks = 0;
  return SASL_OK;
}
int sasl_server_done(void) {
  if (mock_server_initializations == 0)
    return SASL_NOTINIT;
  if (--mock_server_initializations != 0)
    return SASL_CONTINUE;
  mock_server_global_callbacks = 0;
  return SASL_OK;
}

int sasl_client_new(const char *service, const char *server_name,
                    const char *local_endpoint, const char *remote_endpoint,
                    const sasl_callback_t *callbacks, unsigned flags,
                    sasl_conn_t **connection) {
  (void)service;
  (void)server_name;
  (void)local_endpoint;
  (void)remote_endpoint;
  (void)flags;
  mock_callbacks = callbacks;
  if (connection != 0)
    *connection = (sasl_conn_t *)&mock_connection;
  return SASL_OK;
}

int sasl_server_new(const char *service, const char *server_name,
                    const char *user_realm, const char *local_endpoint,
                    const char *remote_endpoint,
                    const sasl_callback_t *callbacks, unsigned flags,
                    sasl_conn_t **connection) {
  (void)service;
  (void)server_name;
  (void)user_realm;
  (void)local_endpoint;
  (void)remote_endpoint;
  (void)callbacks;
  (void)flags;
  if (connection != 0)
    *connection = (sasl_conn_t *)&mock_connection;
  return SASL_OK;
}

int sasl_client_start(sasl_conn_t *connection, const char *mechanisms,
                      sasl_interact_t **interactions, const char **output,
                      unsigned *output_length, const char **mechanism) {
  (void)connection;
  (void)mechanisms;
  if (output != 0)
    *output = 0;
  if (output_length != 0)
    *output_length = 0;
  if (mechanism != 0)
    *mechanism = 0;
  if (mock_start_calls == 0) {
    if (interactions == 0 || *interactions != 0)
      return SASL_BADPARAM;
    mock_start_interactions[0].id = SASL_CB_AUTHNAME;
    mock_start_interactions[0].result = "initial";
    mock_start_interactions[0].len = 7;
    mock_start_interactions[1].id = SASL_CB_LIST_END;
    *interactions = mock_start_interactions;
    mock_start_calls += 1;
    return SASL_INTERACT;
  }
  if (interactions == 0 || *interactions != mock_start_interactions ||
      mock_start_interactions[0].result == 0 ||
      mock_start_interactions[0].len != 6 ||
      memcmp(mock_start_interactions[0].result, "answer", 6) != 0)
    return SASL_BADPARAM;
  *interactions = 0;
  if (output != 0)
    *output = "started";
  if (output_length != 0)
    *output_length = 7;
  mock_start_calls += 1;
  return SASL_OK;
}

int sasl_client_step(sasl_conn_t *connection, const char *input,
                     unsigned input_length, sasl_interact_t **interactions,
                     const char **output, unsigned *output_length) {
  unsigned i;
  (void)connection;
  (void)input;
  (void)input_length;
  if (output != 0)
    *output = 0;
  if (output_length != 0)
    *output_length = 0;
  if (mock_step_calls == 0) {
    if (interactions == 0 || *interactions != 0)
      return SASL_BADPARAM;
    for (i = 0; i < 300; ++i) {
      mock_step_interactions[i].id = SASL_CB_PASS;
      mock_step_interactions[i].result = "initial";
      mock_step_interactions[i].len = 7;
    }
    mock_step_interactions[300].id = SASL_CB_LIST_END;
    *interactions = mock_step_interactions;
    mock_step_calls += 1;
    return SASL_INTERACT;
  }
  if (interactions == 0 || *interactions != mock_step_interactions ||
      mock_step_interactions[0].result == 0 ||
      mock_step_interactions[0].len != 6 ||
      memcmp(mock_step_interactions[0].result, "answer", 6) != 0)
    return SASL_BADPARAM;
  *interactions = 0;
  if (output != 0)
    *output = "stepped";
  if (output_length != 0)
    *output_length = 7;
  mock_step_calls += 1;
  return SASL_OK;
}

int sasl_server_start(sasl_conn_t *connection, const char *mechanism,
                      const char *input, unsigned input_length,
                      const char **output, unsigned *output_length) {
  (void)connection;
  (void)mechanism;
  (void)input;
  (void)input_length;
  if (output != 0)
    *output = 0;
  if (output_length != 0)
    *output_length = 0;
  return SASL_OK;
}

int sasl_server_step(sasl_conn_t *connection, const char *input,
                     unsigned input_length, const char **output,
                     unsigned *output_length) {
  return sasl_server_start(connection, 0, input, input_length, output,
                           output_length);
}

int sasl_listmech(sasl_conn_t *connection, const char *user, const char *prefix,
                  const char *separator, const char *suffix,
                  const char **result, unsigned *result_length, int *count) {
  (void)connection;
  (void)user;
  (void)prefix;
  (void)separator;
  (void)suffix;
  if (result != 0)
    *result = 0;
  if (result_length != 0)
    *result_length = 0;
  if (count != 0)
    *count = 0;
  return SASL_OK;
}

int sasl_encode(sasl_conn_t *connection, const char *input,
                unsigned input_length, const char **output,
                unsigned *output_length) {
  return sasl_server_start(connection, 0, input, input_length, output,
                           output_length);
}

int sasl_decode(sasl_conn_t *connection, const char *input,
                unsigned input_length, const char **output,
                unsigned *output_length) {
  return sasl_server_start(connection, 0, input, input_length, output,
                           output_length);
}

int sasl_setprop(sasl_conn_t *connection, int property, const void *value) {
  (void)connection;
  (void)property;
  (void)value;
  return SASL_OK;
}

int sasl_getprop(sasl_conn_t *connection, int property, const void **value) {
  (void)connection;
  if (value == 0)
    return SASL_BADPARAM;
  if (property == SASL_AUTHSOURCE)
    *value = mock_source;
  else if (property == SASL_MECHNAME)
    *value = mock_mechanism;
  else if (property == SASL_CALLBACK)
    *value = mock_callbacks;
  else if (property == SASL_GETOPTCTX) {
    const sasl_callback_t *callback = mock_callback(SASL_CB_GETOPT);
    *value = callback == 0 ? 0 : callback->context;
  } else if (property == SASL_DELEGATEDCREDS)
    *value = mock_delegated_credential == 0
                 ? 0
                 : (strcmp(mock_source, "cpkt-custom") == 0
                        ? mock_delegated_credential
                        : &mock_delegated_credential);
  else
    return SASL_BADPARAM;
  if (*value == 0)
    return SASL_NOTDONE;
  return SASL_OK;
}

int sasl_set_path(int type, char *path) {
  (void)type;
  (void)path;
  return SASL_OK;
}

const char *sasl_errdetail(sasl_conn_t *connection) {
  (void)connection;
  return "mock SASL detail";
}

const char *sasl_errstring(int status, const char *languages,
                           const char **language) {
  (void)status;
  (void)languages;
  if (language != 0)
    *language = 0;
  return "mock SASL error";
}

void sasl_version_info(const char **implementation, const char **version,
                       int *major, int *minor, int *step, int *patch) {
  if (implementation != 0)
    *implementation = "mock";
  if (version != 0)
    *version = "0";
  if (major != 0)
    *major = 0;
  if (minor != 0)
    *minor = 0;
  if (step != 0)
    *step = 0;
  if (patch != 0)
    *patch = 0;
}

void sasl_dispose(sasl_conn_t **connection) {
  if (mock_delegated_credential != 0) {
    ++mock_provider_credential_releases;
    mock_delegated_credential = 0;
  }
  if (connection != 0)
    *connection = 0;
}

int main(void) {
  const sasl_callback_t *callback;
  mock_callback_bridge callback_bridge;
  cpkt_sasl_callbacks callbacks;
  cpkt_sasl *client;
  const cpkt_sasl_callbacks *original_callbacks;
  void *option_context;
  cpkt_gss_credential *taken_credential;
  const void *generic_payload;
  cpkt_sasl_interaction *interactions;
  const char *result;
  const char *output;
  const char *mechanism;
  sasl_secret_t *native_secret;
  unsigned length;
  unsigned long output_length;
  int callback_calls;
  int status;

  memset(&callbacks, 0, sizeof(callbacks));
  callback_calls = 0;
  callbacks.context = &callback_calls;
  callbacks.simple = mock_simple;
  callbacks.option = mock_option;
  callbacks.language = mock_extended_simple;
  callbacks.client_nonce = mock_extended_simple;
  callbacks.challenge_no_echo = mock_extended_challenge;
  callbacks.secret = mock_secret;
  status = CPKT_SASL_FAIL;
  client = cpkt_sasl_client_new("imap", "mail.example.test", 0, 0, &callbacks,
                                0, &status);
  if (client == 0 || status != CPKT_SASL_OK)
    return 1;
  original_callbacks = 0;
  option_context = 0;
  if (client->get_callback_record(client, &original_callbacks) !=
          CPKT_SASL_OK ||
      original_callbacks == 0 || original_callbacks == &callbacks ||
      original_callbacks->context != &callback_calls ||
      original_callbacks->option != mock_option ||
      client->get_option_context(client, &option_context) != CPKT_SASL_OK ||
      option_context != &callback_calls)
    return 35;
  if (mock_option_boundaries(mock_callback(SASL_CB_GETOPT)) != 0)
    return 36;
  callback = mock_callback(SASL_CB_AUTHNAME);
  if (callback == 0 || callback->proc == 0)
    return 2;
  callback_bridge.generic = callback->proc;
  result = 0;
  length = 0;
  if (callback_bridge.simple(callback->context, SASL_CB_AUTHNAME, &result,
                             &length) != SASL_OK ||
      result == 0 || strcmp(result, "facade-authentication-user") != 0 ||
      length != strlen(result) || callback_calls != 1)
    return 3;
  callback = mock_callback(SASL_CB_LANGUAGE);
  if (callback == 0 || callback->proc == 0)
    return 24;
  callback_bridge.generic = callback->proc;
  if (callback_bridge.simple(callback->context, SASL_CB_LANGUAGE, &result,
                             &length) != SASL_OK ||
      strcmp(result, "en") != 0 || length != 2)
    return 25;
  callback = mock_callback(SASL_CB_CNONCE);
  if (callback == 0 || callback->proc == 0)
    return 26;
  callback_bridge.generic = callback->proc;
  if (callback_bridge.simple(callback->context, SASL_CB_CNONCE, &result,
                             &length) != SASL_OK ||
      strcmp(result, "nonce") != 0 || length != 5)
    return 27;
  callback = mock_callback(SASL_CB_NOECHOPROMPT);
  if (callback == 0 || callback->proc == 0)
    return 28;
  callback_bridge.generic = callback->proc;
  if (callback_bridge.challenge(callback->context, SASL_CB_NOECHOPROMPT,
                                "challenge", "prompt", 0, &result,
                                &length) != SASL_OK ||
      strcmp(result, "private") != 0 || length != 7 ||
      mock_extended_callback_calls != 3)
    return 29;
  callback = mock_callback(SASL_CB_PASS);
  if (callback == 0 || callback->proc == 0)
    return 8;
  callback_bridge.generic = callback->proc;
  native_secret = 0;
  if (callback_bridge.secret((sasl_conn_t *)&mock_connection, callback->context,
                             SASL_CB_PASS, &native_secret) != SASL_OK ||
      native_secret == 0 || native_secret->len != 6 ||
      memcmp(native_secret->data, "secret", 6) != 0 ||
      native_secret->data[6] != 0 || callback_calls != 2)
    return 9;
  mock_password[0] = 'S';
  if (native_secret->data[0] != 's')
    return 10;
  mock_secret_mode = 1;
  native_secret = (sasl_secret_t *)&mock_connection;
  if (callback_bridge.secret((sasl_conn_t *)&mock_connection, callback->context,
                             SASL_CB_PASS, &native_secret) != SASL_OK ||
      native_secret != 0 || callback_calls != 3)
    return 11;
  mock_secret_mode = 2;
  if (callback_bridge.secret((sasl_conn_t *)&mock_connection, callback->context,
                             SASL_CB_PASS, &native_secret) != SASL_BADPARAM ||
      native_secret != 0)
    return 12;
  mock_secret_mode = 3;
  if (callback_bridge.secret((sasl_conn_t *)&mock_connection, callback->context,
                             SASL_CB_PASS, &native_secret) != SASL_OK ||
      native_secret == 0 || native_secret->len != 0 ||
      native_secret->data[0] != 0)
    return 13;
  interactions = 0;
  output = 0;
  output_length = 0;
  mechanism = 0;
  if (client->start(client, "MOCK", &interactions, &output, &output_length,
                    &mechanism) != CPKT_SASL_INTERACT ||
      interactions == 0 || interactions[0].id != SASL_CB_AUTHNAME ||
      interactions[0].result == 0 ||
      strcmp((const char *)interactions[0].result, "initial") != 0 ||
      interactions[0].result_byte_count != 7)
    return 4;
  interactions[0].result = "answer";
  interactions[0].result_byte_count = 6;
  if (client->start(client, "MOCK", &interactions, &output, &output_length,
                    &mechanism) != CPKT_SASL_OK ||
      interactions != 0 || output == 0 || output_length != 7 ||
      mock_start_calls != 2)
    return 5;
  interactions = 0;
  if (client->step(client, "input", 5, &interactions, &output,
                   &output_length) != CPKT_SASL_INTERACT ||
      interactions == 0 || interactions[0].id != SASL_CB_PASS ||
      interactions[299].id != SASL_CB_PASS ||
      interactions[300].id != SASL_CB_LIST_END || interactions[0].result == 0 ||
      strcmp((const char *)interactions[0].result, "initial") != 0 ||
      interactions[0].result_byte_count != 7)
    return 6;
  interactions[0].result = "answer";
  interactions[0].result_byte_count = 6;
  if (client->step(client, "input", 5, &interactions, &output,
                   &output_length) != CPKT_SASL_OK ||
      interactions != 0 || output == 0 || output_length != 7 ||
      mock_step_calls != 2)
    return 7;
  taken_credential = 0;
  generic_payload = 0;
  if (client->get_delegated_payload(client, &generic_payload) != CPKT_SASL_OK ||
      generic_payload != &mock_delegated_credential)
    return 33;
  if (client->get_delegated_credentials(client, &taken_credential) !=
          CPKT_SASL_OK ||
      taken_credential != (cpkt_gss_credential *)&mock_connection ||
      mock_delegated_credential != 0)
    return 30;
  taken_credential = (cpkt_gss_credential *)&mock_connection;
  if (client->get_delegated_credentials(client, &taken_credential) !=
          CPKT_SASL_NOTDONE ||
      taken_credential != 0)
    return 31;
  ++mock_application_credential_releases;
  client->close(client);
  if (mock_provider_credential_releases != 0 ||
      mock_application_credential_releases != 1)
    return 32;
  mock_source = "cpkt-custom";
  mock_mechanism = "CPKT-CUSTOM";
  mock_delegated_credential = (void *)1;
  client =
      cpkt_sasl_client_new("imap", "mail.example.test", 0, 0, 0, 0, &status);
  generic_payload = 0;
  taken_credential = (cpkt_gss_credential *)&mock_connection;
  if (client == 0 || status != CPKT_SASL_OK ||
      client->get_delegated_payload(client, &generic_payload) != CPKT_SASL_OK ||
      generic_payload != (void *)1 ||
      client->get_delegated_credentials(client, &taken_credential) !=
          CPKT_SASL_BADPROT ||
      taken_credential != 0 || mock_delegated_credential != (void *)1)
    return 34;
  mock_delegated_credential = 0;
  client->close(client);
  callbacks.secret = 0;
  callback_calls = 0;
  if (cpkt_sasl_client_initialize(&callbacks) != CPKT_SASL_OK ||
      cpkt_sasl_client_initialize(0) != CPKT_SASL_OK)
    return 14;
  if (mock_option_boundaries(mock_find_callback(mock_client_global_callbacks,
                                                SASL_CB_GETOPT)) != 0)
    return 37;
  callback = mock_find_callback(mock_client_global_callbacks, SASL_CB_AUTHNAME);
  if (callback == 0 || callback->proc == 0)
    return 15;
  callback_bridge.generic = callback->proc;
  result = 0;
  length = 0;
  if (callback_bridge.simple(callback->context, SASL_CB_AUTHNAME, &result,
                             &length) != SASL_OK ||
      callback_calls != 1 || result == 0 ||
      strcmp(result, "facade-authentication-user") != 0)
    return 16;
  if (cpkt_sasl_client_finish() != CPKT_SASL_CONTINUE)
    return 17;
  callback_calls = 0;
  if (callback_bridge.simple(callback->context, SASL_CB_AUTHNAME, &result,
                             &length) != SASL_OK ||
      callback_calls != 1 || cpkt_sasl_client_finish() != CPKT_SASL_OK)
    return 22;
  callback_calls = 0;
  if (cpkt_sasl_server_initialize(&callbacks, "test") != CPKT_SASL_OK ||
      cpkt_sasl_server_initialize(0, "test") != CPKT_SASL_OK)
    return 18;
  if (mock_option_boundaries(mock_find_callback(mock_server_global_callbacks,
                                                SASL_CB_GETOPT)) != 0)
    return 38;
  callback = mock_find_callback(mock_server_global_callbacks, SASL_CB_AUTHNAME);
  if (callback == 0 || callback->proc == 0)
    return 19;
  callback_bridge.generic = callback->proc;
  result = 0;
  length = 0;
  if (callback_bridge.simple(callback->context, SASL_CB_AUTHNAME, &result,
                             &length) != SASL_OK ||
      callback_calls != 1 || result == 0 ||
      strcmp(result, "facade-authentication-user") != 0)
    return 20;
  if (cpkt_sasl_server_finish() != CPKT_SASL_CONTINUE)
    return 21;
  callback_calls = 0;
  if (callback_bridge.simple(callback->context, SASL_CB_AUTHNAME, &result,
                             &length) != SASL_OK ||
      callback_calls != 1 || cpkt_sasl_server_finish() != CPKT_SASL_OK)
    return 23;
  return 0;
}
