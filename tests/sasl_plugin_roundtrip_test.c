#include <cpkt/sasl_plugin.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "SASL plugin regression at %d: %s\n", __LINE__,          \
              #condition);                                                     \
      return CPKT_SASL_FAIL;                                                   \
    }                                                                          \
  } while (0)

static int client_cookie, server_cookie, ignored_cookie;
static void *expected_context;
static const cpkt_sasl_plugin_utils *shared_utils, *role_utils[2];
static int observations, expired[2], expired_calls;
static int option_length_override;
static unsigned long option_reported_length;

static int option(void *context, const char *plugin, const char *name,
                  const char **result, unsigned long *length) {
  (void)plugin;
  if ((context == &client_cookie && expired[0]) ||
      (context == &server_cookie && expired[1])) {
    ++expired_calls;
    return CPKT_SASL_FAIL;
  }
  if ((context != &client_cookie && context != &server_cookie) ||
      strcmp(name, "cpkt-regression-option") != 0)
    return CPKT_SASL_FAIL;
  *result = context == &client_cookie ? "client" : "server";
  if (length != NULL)
    *length = option_length_override ? option_reported_length
                                     : (unsigned long)strlen(*result);
  return CPKT_SASL_OK;
}

static int inspect(const cpkt_sasl_plugin_utils *utils) {
  const char *value = NULL;
  unsigned long length = 0;
  int status;
  if (utils == NULL || utils->option_context != expected_context ||
      utils->option == NULL)
    return 0;
  status =
      utils->option(utils, NULL, "cpkt-regression-option", &value, &length);
  if (expected_context == NULL)
    return status == CPKT_SASL_FAIL;
  return status == CPKT_SASL_OK && value != NULL &&
         strcmp(value, expected_context == &client_cookie ? "client"
                                                          : "server") == 0 &&
         length == (unsigned long)strlen(value);
}

static int option_lengths(const cpkt_sasl_plugin_utils *utils) {
  unsigned long lengths[7], length;
  const char *value;
  int count, index, null_length, status;

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
  option_length_override = 1;
  for (index = 0; index < count; ++index)
    for (null_length = 0; null_length < 2; ++null_length) {
      option_reported_length = lengths[index];
      value = NULL;
      length = 37;
      status = utils->option(utils, NULL, "cpkt-regression-option", &value,
                             null_length ? NULL : &length);
      if (lengths[index] > UINT_MAX) {
        /* Cyrus may replace a rejected callback status through its normal
         * configuration fallback. It must not report a truncated success. */
        CHECK(status != CPKT_SASL_OK);
        CHECK(length == (null_length ? 37UL : 0UL));
      } else {
        CHECK(status == CPKT_SASL_OK);
        CHECK(value != NULL);
        CHECK(strcmp(value, expected_context == &client_cookie
                                ? "client"
                                : "server") == 0);
        CHECK(length == (null_length ? 37UL : lengths[index]));
      }
    }
  option_length_override = 0;
  return 0;
}

/* Deliberately reject registration after examining the real factory utilities.
 * The persistent utility table remains owned by the active SASL runtime. */
static int inspect_canon(void *context, const cpkt_sasl_plugin_utils *utils,
                         int maximum, int *version,
                         const cpkt_sasl_canonicalizer_plugin **plugin,
                         const char *name) {
  (void)context;
  (void)maximum;
  (void)version;
  (void)plugin;
  (void)name;
  if (inspect(utils)) {
    ++observations;
    shared_utils = utils;
  }
  return CPKT_SASL_FAIL;
}

static int inspect_aux(void *context, const cpkt_sasl_plugin_utils *utils,
                       int maximum, int *version,
                       const cpkt_sasl_auxiliary_plugin **plugin,
                       const char *name) {
  (void)context;
  (void)maximum;
  (void)version;
  (void)plugin;
  (void)name;
  if (inspect(utils) && utils == shared_utils)
    ++observations;
  return CPKT_SASL_FAIL;
}

static int inspect_client(void *context, const cpkt_sasl_plugin_utils *utils,
                          int maximum, int *version,
                          const cpkt_sasl_client_plugin **plugins, int *count) {
  (void)context;
  (void)maximum;
  (void)version;
  (void)plugins;
  (void)count;
  if (inspect(utils)) {
    ++observations;
    role_utils[0] = utils;
  }
  return CPKT_SASL_FAIL;
}

static int inspect_server(void *context, const cpkt_sasl_plugin_utils *utils,
                          int maximum, int *version,
                          const cpkt_sasl_server_plugin **plugins, int *count) {
  (void)context;
  (void)maximum;
  (void)version;
  (void)plugins;
  (void)count;
  if (inspect(utils)) {
    ++observations;
    role_utils[1] = utils;
  }
  return CPKT_SASL_FAIL;
}

static int initialize(int server, const cpkt_sasl_callbacks *callbacks) {
  return server ? cpkt_sasl_server_initialize(callbacks, "cpkt-context-test")
                : cpkt_sasl_client_initialize(callbacks);
}

static int finish(int server) {
  return server ? cpkt_sasl_server_finish() : cpkt_sasl_client_finish();
}

static int inspect_shared(void) {
  int previous = observations;
  CHECK(cpkt_sasl_canonicalizer_add_plugin("cpkt-context-test", inspect_canon,
                                           NULL) == CPKT_SASL_FAIL);
  CHECK(cpkt_sasl_auxiliary_add_plugin("cpkt-context-test", inspect_aux,
                                       NULL) == CPKT_SASL_FAIL);
  CHECK(observations == previous + 2);
  return 0;
}

static int global_options(void) {
  cpkt_sasl_callbacks callbacks[2], ignored;
  int first, absent, latest_finished, role, previous, status;
  for (first = 0; first < 2; ++first) {
    for (absent = 0; absent < 2; ++absent) {
      for (latest_finished = 0; latest_finished < 2; ++latest_finished) {
        /* Native full shutdown resets its plugin path. Keep every cycle
         * independent of host-installed plugins in the shared linkage too. */
        CHECK(cpkt_sasl_set_path(CPKT_SASL_PATH_PLUGIN,
                                 "/cpkt-no-external-sasl-plugins") ==
              CPKT_SASL_OK);
        expired[0] = expired[1] = expired_calls = 0;
        shared_utils = NULL;
        role_utils[0] = role_utils[1] = NULL;
        memset(callbacks, 0, sizeof(callbacks));
        callbacks[0].context = &client_cookie;
        callbacks[1].context = &server_cookie;
        callbacks[0].option = option;
        callbacks[1].option = option;
        if (absent)
          callbacks[1 - first].option = NULL;
        ignored = callbacks[first];
        ignored.context = &ignored_cookie;
        status = initialize(first, &callbacks[first]);
        if (status != CPKT_SASL_OK)
          fprintf(stderr,
                  "initialize role=%d absent=%d latest_finished=%d status=%d\n",
                  first, absent, latest_finished, status);
        CHECK(status == CPKT_SASL_OK);
        expected_context = callbacks[first].context;
        CHECK(inspect_shared() == 0);
        CHECK(initialize(first, &ignored) == CPKT_SASL_OK);
        CHECK(inspect(shared_utils));
        CHECK(finish(first) == CPKT_SASL_CONTINUE);
        CHECK(initialize(1 - first, &callbacks[1 - first]) == CPKT_SASL_OK);
        expected_context = absent ? NULL : callbacks[1 - first].context;
        CHECK(inspect(shared_utils));
        CHECK(inspect_shared() == 0);
        for (role = 0; role < 2; ++role) {
          expected_context =
              callbacks[role].option == NULL ? NULL : callbacks[role].context;
          previous = observations;
          if (role)
            CHECK(cpkt_sasl_server_add_plugin("cpkt-context-test",
                                              inspect_server,
                                              NULL) == CPKT_SASL_FAIL);
          else
            CHECK(cpkt_sasl_client_add_plugin("cpkt-context-test",
                                              inspect_client,
                                              NULL) == CPKT_SASL_FAIL);
          CHECK(observations == previous + 1);
          CHECK(inspect(role_utils[role]));
          if (expected_context != NULL)
            CHECK(option_lengths(role_utils[role]) == 0);
        }
        expected_context = absent ? NULL : callbacks[1 - first].context;
        if (expected_context != NULL)
          CHECK(option_lengths(shared_utils) == 0);
        CHECK(initialize(first, &ignored) == CPKT_SASL_OK);
        CHECK(inspect(shared_utils));
        CHECK(finish(first) == CPKT_SASL_CONTINUE);
        role = latest_finished ? 1 - first : first;
        CHECK(finish(role) == CPKT_SASL_OK);
        expired[role] = 1;
        if (latest_finished)
          expected_context = NULL;
        CHECK(shared_utils->option_context == expected_context);
        CHECK(inspect(shared_utils));
        CHECK(expired_calls == 0);
        CHECK(finish(1 - role) == CPKT_SASL_OK);
      }
    }
  }
  return 0;
}

static int phase, encoder_cookie, decoder_cookie;

static int encode(cpkt_sasl_plugin_output *self, const cpkt_sasl_iov *vectors,
                  size_t count, const char **output, unsigned long *length) {
  if ((self->encode_context != &client_cookie &&
       self->encode_context != &encoder_cookie) ||
      count != 1 || vectors == NULL || vectors[0].byte_count != 3 ||
      memcmp(vectors[0].data, "abc", 3) != 0)
    return CPKT_SASL_BADPARAM;
  *output = self->encode_context == &client_cookie ? "old" : "new";
  *length = 3;
  return CPKT_SASL_OK;
}

static int decode(cpkt_sasl_plugin_output *self, const char *input,
                  unsigned long input_length, const char **output,
                  unsigned long *length) {
  if ((self->decode_context != &server_cookie &&
       self->decode_context != &decoder_cookie) ||
      input_length != 3 || memcmp(input, "abc", 3) != 0)
    return CPKT_SASL_BADPARAM;
  *output = self->decode_context == &server_cookie ? "old" : "new";
  *length = 3;
  return CPKT_SASL_OK;
}

static int encode_next(cpkt_sasl_plugin_output *self,
                       const cpkt_sasl_iov *vectors, size_t count,
                       const char **output, unsigned long *length) {
  return encode(self, vectors, count, output, length);
}
static int decode_next(cpkt_sasl_plugin_output *self, const char *input,
                       unsigned long input_length, const char **output,
                       unsigned long *length) {
  return decode(self, input, input_length, output, length);
}

static int layer_new(void *context, cpkt_sasl_client_params *params,
                     void **connection) {
  (void)context;
  (void)params;
  phase = 0;
  *connection = &phase;
  return CPKT_SASL_OK;
}

static int layer_step(void *context, cpkt_sasl_client_params *params,
                      const char *input, unsigned long input_length,
                      cpkt_sasl_interaction **interactions, const char **output,
                      unsigned long *length, cpkt_sasl_plugin_output *out) {
  cpkt_sasl_iov vector;
  const char *value;
  unsigned long bytes;
  (void)params;
  (void)input;
  (void)input_length;
  (void)interactions;
  CHECK(context == &phase);
  if (phase == 0) {
    out->encode = encode;
    out->decode = decode;
    out->encode_context = &client_cookie;
    out->decode_context = &server_cookie;
  } else {
    CHECK(out->encode == (phase == 1 ? encode : encode_next));
    CHECK(out->decode == (phase <= 2 ? decode : decode_next));
    CHECK(out->encode_context ==
          (phase == 1 ? &client_cookie : &encoder_cookie));
    CHECK(out->decode_context ==
          (phase <= 2 ? &server_cookie : &decoder_cookie));
    if (phase == 1) {
      out->encode = encode_next;
      out->encode_context = &encoder_cookie;
    } else if (phase == 2) {
      out->decode = decode_next;
      out->decode_context = &decoder_cookie;
    }
  }
  vector.data = "abc";
  vector.byte_count = 3;
  CHECK(out->encode(out, &vector, 1, &value, &bytes) == CPKT_SASL_OK);
  CHECK(bytes == 3 && strcmp(value, phase == 0 ? "old" : "new") == 0);
  CHECK(out->decode(out, "abc", 3, &value, &bytes) == CPKT_SASL_OK);
  CHECK(bytes == 3 && strcmp(value, phase < 2 ? "old" : "new") == 0);
  *output = "token";
  *length = 5;
  out->done = phase == 3;
  out->user = out->authentication_identity = "user";
  out->user_length = out->authentication_length = 4;
  out->maximum_output_bytes = 64;
  out->mechanism_ssf = 1;
  ++phase;
  return phase == 4 ? CPKT_SASL_OK : CPKT_SASL_CONTINUE;
}

static int layer_init(void *context, const cpkt_sasl_plugin_utils *utils,
                      int maximum, int *version,
                      const cpkt_sasl_client_plugin **plugins, int *count) {
  static cpkt_sasl_client_plugin plugin;
  static const unsigned long prompts[] = {CPKT_SASL_CALLBACK_LIST_END};
  (void)context;
  (void)utils;
  CHECK(maximum >= CPKT_SASL_CLIENT_PLUGIN_VERSION);
  memset(&plugin, 0, sizeof(plugin));
  plugin.mechanism_name = "CPKT-ROUNDTRIP";
  plugin.maximum_ssf = 1;
  plugin.security_flags = CPKT_SASL_SECURITY_NO_ANONYMOUS;
  plugin.required_prompts = prompts;
  plugin.new_connection = layer_new;
  plugin.step = layer_step;
  *plugins = &plugin;
  *count = 1;
  *version = CPKT_SASL_CLIENT_PLUGIN_VERSION;
  return CPKT_SASL_OK;
}

static int security_callbacks(void) {
  cpkt_sasl *client;
  cpkt_sasl_security_properties properties;
  cpkt_sasl_interaction *interactions = NULL;
  cpkt_sasl_iov vector;
  const char *output, *mechanism;
  unsigned long length;
  int status, step;
  CHECK(cpkt_sasl_client_initialize(NULL) == CPKT_SASL_OK);
  CHECK(cpkt_sasl_client_add_plugin("cpkt-roundtrip", layer_init, NULL) ==
        CPKT_SASL_OK);
  client =
      cpkt_sasl_client_new("test", "localhost", NULL, NULL, NULL, 0, &status);
  CHECK(client != NULL && status == CPKT_SASL_OK);
  memset(&properties, 0, sizeof(properties));
  properties.maximum_ssf = 1;
  properties.maximum_buffer_bytes = 1024;
  CHECK(client->set_security_properties(client, &properties) == CPKT_SASL_OK);
  CHECK(client->start(client, "CPKT-ROUNDTRIP", &interactions, &output, &length,
                      &mechanism) == CPKT_SASL_CONTINUE);
  for (step = 1; step < 4; ++step) {
    CHECK(
        client->step(client, "challenge", 9, &interactions, &output, &length) ==
        (step == 3 ? CPKT_SASL_OK : CPKT_SASL_CONTINUE));
  }
  CHECK(phase == 4);
  vector.data = "abc";
  vector.byte_count = 3;
  CHECK(client->encode_vector(client, &vector, 1, &output, &length) ==
        CPKT_SASL_OK);
  CHECK(length == 3 && memcmp(output, "new", 3) == 0);
  CHECK(client->decode(client, "abc", 3, &output, &length) == CPKT_SASL_OK);
  CHECK(length == 3 && memcmp(output, "new", 3) == 0);
  client->close(client);
  CHECK(cpkt_sasl_client_finish() == CPKT_SASL_OK);
  return 0;
}

int main(int argc, char **argv) {
  CHECK(argc == 2);
  CHECK(cpkt_sasl_set_path(CPKT_SASL_PATH_PLUGIN,
                           "/cpkt-no-external-sasl-plugins") == CPKT_SASL_OK);
  if (strcmp(argv[1], "global_options") == 0)
    return global_options();
  if (strcmp(argv[1], "security_callbacks") == 0)
    return security_callbacks();
  return 1;
}
