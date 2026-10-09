#include <cpkt/sasl_plugin.h>

#include <limits.h>
#include <sasl/sasl.h>
#include <stdio.h>
#include <stdlib.h>
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

static unsigned long canonical_reported_length;
static cpkt_sasl *canonical_receiver;
static int canonical_calls;

static int canonical_callback(cpkt_sasl *receiver, void *context,
                              const char *input, unsigned long length,
                              unsigned long flags, const char *realm,
                              char *output, unsigned long capacity,
                              unsigned long *output_length) {
  CHECK(receiver == canonical_receiver && context == &canonical_calls);
  CHECK(length == 4 && memcmp(input, "user", 4) == 0 && realm == NULL);
  CHECK(flags == (CPKT_SASL_CANONICALIZE_AUTHENTICATION_ID |
                  CPKT_SASL_CANONICALIZE_AUTHORIZATION_ID));
  CHECK(capacity >= 5 && output_length != NULL);
  ++canonical_calls;
  memcpy(output, "user", 5);
  *output_length = canonical_reported_length;
  return CPKT_SASL_OK;
}

static int canonical_step(void *context, cpkt_sasl_client_params *params,
                          const char *input, unsigned long input_length,
                          cpkt_sasl_interaction **interactions,
                          const char **output, unsigned long *length,
                          cpkt_sasl_plugin_output *out) {
  cpkt_sasl_plugin_output canonical, saved;
  int status;
  (void)input;
  (void)input_length;
  (void)interactions;
  CHECK(context == &phase && params->canonicalize != NULL);
  memset(&canonical, 0, sizeof(canonical));
  canonical.user = canonical.authentication_identity = "unchanged";
  canonical.user_length = canonical.authentication_length = 9;
  memcpy(&saved, &canonical, sizeof(saved));
  status = params->canonicalize(params, "user", 4,
                                CPKT_SASL_CANONICALIZE_AUTHENTICATION_ID |
                                    CPKT_SASL_CANONICALIZE_AUTHORIZATION_ID,
                                &canonical);
  CHECK(canonical_calls == 1);
  if (canonical_reported_length > UINT_MAX) {
    CHECK(status == CPKT_SASL_BADPARAM);
    CHECK(memcmp(&canonical, &saved, sizeof(saved)) == 0);
  } else {
    CHECK(status == CPKT_SASL_OK);
    CHECK(canonical.user_length == 4 && canonical.authentication_length == 4);
    CHECK(memcmp(canonical.user, "user", 4) == 0);
    CHECK(memcmp(canonical.authentication_identity, "user", 4) == 0);
  }
  *output = "token";
  *length = 5;
  out->done = 1;
  out->user = out->authentication_identity = "user";
  out->user_length = out->authentication_length = 4;
  return CPKT_SASL_OK;
}

static int canonical_init(void *context, const cpkt_sasl_plugin_utils *utils,
                          int maximum, int *version,
                          const cpkt_sasl_client_plugin **plugins, int *count) {
  static cpkt_sasl_client_plugin plugin;
  static const unsigned long prompts[] = {CPKT_SASL_CALLBACK_LIST_END};
  (void)context;
  (void)utils;
  CHECK(maximum >= CPKT_SASL_CLIENT_PLUGIN_VERSION);
  memset(&plugin, 0, sizeof(plugin));
  plugin.mechanism_name = "CPKT-CANON-LENGTH";
  plugin.security_flags = CPKT_SASL_SECURITY_NO_ANONYMOUS;
  plugin.required_prompts = prompts;
  plugin.new_connection = layer_new;
  plugin.step = canonical_step;
  *plugins = &plugin;
  *count = 1;
  *version = CPKT_SASL_CLIENT_PLUGIN_VERSION;
  return CPKT_SASL_OK;
}

static int canonicalization_lengths(void) {
  cpkt_sasl_callbacks callbacks;
  cpkt_sasl_interaction *interactions;
  const char *output, *mechanism;
  unsigned long lengths[4], length;
  int count, global, index, status;
  lengths[0] = 4;
  count = 1;
#if ULONG_MAX > UINT_MAX
  lengths[count++] = (unsigned long)UINT_MAX + 1UL;
  lengths[count++] = (unsigned long)UINT_MAX + 2UL;
  lengths[count++] = ULONG_MAX;
#endif
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.context = &canonical_calls;
  callbacks.canonicalize = canonical_callback;
  for (global = 0; global < 2; ++global) {
    CHECK(cpkt_sasl_set_path(CPKT_SASL_PATH_PLUGIN,
                             "/cpkt-no-external-sasl-plugins") == CPKT_SASL_OK);
    CHECK(cpkt_sasl_client_initialize(global ? &callbacks : NULL) ==
          CPKT_SASL_OK);
    CHECK(cpkt_sasl_client_add_plugin("cpkt-canon-length", canonical_init,
                                      NULL) == CPKT_SASL_OK);
    for (index = 0; index < count; ++index) {
      canonical_reported_length = lengths[index];
      canonical_calls = 0;
      canonical_receiver =
          cpkt_sasl_client_new("test", "localhost", NULL, NULL,
                               global ? NULL : &callbacks, 0, &status);
      CHECK(canonical_receiver != NULL && status == CPKT_SASL_OK);
      interactions = NULL;
      CHECK(canonical_receiver->start(canonical_receiver, "CPKT-CANON-LENGTH",
                                      &interactions, &output, &length,
                                      &mechanism) == CPKT_SASL_OK);
      CHECK(canonical_calls == 1 && length == 5);
      CHECK(output != NULL && memcmp(output, "token", 5) == 0);
      canonical_receiver->close(canonical_receiver);
    }
    CHECK(cpkt_sasl_client_finish() == CPKT_SASL_OK);
  }
  return 0;
}

static cpkt_sasl_interaction *interaction_list;
static int interaction_count, interaction_phase;

static int interaction_new(void *context, cpkt_sasl_client_params *params,
                           void **connection) {
  (void)context;
  (void)params;
  interaction_phase = 0;
  *connection = &interaction_phase;
  return CPKT_SASL_OK;
}

static int interaction_step(void *context, cpkt_sasl_client_params *params,
                            const char *input, unsigned long input_length,
                            cpkt_sasl_interaction **interactions,
                            const char **output, unsigned long *length,
                            cpkt_sasl_plugin_output *out) {
  int i;
  (void)params;
  (void)input;
  (void)input_length;
  CHECK(context == &interaction_phase && interactions != NULL);
  if (interaction_phase++ == 0) {
    *interactions = interaction_list;
    *output = NULL;
    *length = 0;
    return CPKT_SASL_INTERACT;
  }
  CHECK(*interactions == interaction_list);
  for (i = 0; i < interaction_count; ++i) {
    CHECK(interaction_list[i].result_byte_count == 6);
    CHECK(memcmp(interaction_list[i].result, "answer", 6) == 0);
  }
  *interactions = NULL;
  *output = "token";
  *length = 5;
  out->done = 1;
  out->user = out->authentication_identity = "user";
  out->user_length = out->authentication_length = 4;
  return CPKT_SASL_OK;
}

static int interaction_init(void *context, const cpkt_sasl_plugin_utils *utils,
                            int maximum, int *version,
                            const cpkt_sasl_client_plugin **plugins,
                            int *count) {
  static cpkt_sasl_client_plugin plugin;
  static const unsigned long prompts[] = {CPKT_SASL_CALLBACK_LIST_END};
  (void)context;
  (void)utils;
  CHECK(maximum >= CPKT_SASL_CLIENT_PLUGIN_VERSION);
  memset(&plugin, 0, sizeof(plugin));
  plugin.mechanism_name = "CPKT-INTERACTION";
  plugin.security_flags = CPKT_SASL_SECURITY_NO_ANONYMOUS;
  plugin.required_prompts = prompts;
  plugin.new_connection = interaction_new;
  plugin.step = interaction_step;
  *plugins = &plugin;
  *count = 1;
  *version = CPKT_SASL_CLIENT_PLUGIN_VERSION;
  return CPKT_SASL_OK;
}

static int interaction_terminator(void) {
  cpkt_sasl *client;
  sasl_conn_t *native;
  sasl_interact_t *native_prompts;
  cpkt_sasl_interaction *public_prompts, saved;
  const char *output, *mechanism;
  unsigned native_length;
  unsigned long length;
  int counts[4], size_cases, size_case, mode, native_api, i, status;

  counts[0] = 0;
  counts[1] = 1;
  counts[2] = 3;
  counts[3] = 1;
  size_cases = 3;
#if ULONG_MAX > UINT_MAX
  size_cases = 4;
#endif
  CHECK(cpkt_sasl_client_initialize(NULL) == CPKT_SASL_OK);
  CHECK(cpkt_sasl_client_add_plugin("cpkt-interaction", interaction_init,
                                    NULL) == CPKT_SASL_OK);
  for (size_case = 0; size_case < size_cases; ++size_case)
    for (mode = 0; mode < 3; ++mode)
      for (native_api = 0; native_api < 2; ++native_api) {
        interaction_count = counts[size_case];
        interaction_list = (cpkt_sasl_interaction *)malloc(
            (size_t)(interaction_count + 1) * sizeof(*interaction_list));
        CHECK(interaction_list != NULL);
        for (i = 0; i < interaction_count; ++i) {
          memset(&interaction_list[i], 0, sizeof(interaction_list[i]));
          interaction_list[i].id = CPKT_SASL_CALLBACK_AUTHENTICATION_NAME;
          interaction_list[i].challenge = "challenge";
          interaction_list[i].prompt = "prompt";
          interaction_list[i].default_result = "default";
          interaction_list[i].result = "initial";
          interaction_list[i].result_byte_count =
              size_case == 3 ? ULONG_MAX : 7;
        }
        if (mode != 2) {
          memset(&interaction_list[interaction_count], 0,
                 sizeof(*interaction_list));
          if (mode == 1) {
            interaction_list[interaction_count].challenge = "unused";
            interaction_list[interaction_count].prompt = "unused";
            interaction_list[interaction_count].default_result = "unused";
            interaction_list[interaction_count].result = "unused";
            interaction_list[interaction_count].result_byte_count = ULONG_MAX;
          }
        }
        /* ID-only termination is valid; mode 2 deliberately leaves the
         * remaining fields uninitialized for memory-check coverage. */
        interaction_list[interaction_count].id = CPKT_SASL_CALLBACK_LIST_END;
        if (mode != 2)
          memcpy(&saved, &interaction_list[interaction_count], sizeof(saved));
        native_prompts = NULL;
        public_prompts = NULL;
        native = NULL;
        client = NULL;
        if (native_api) {
          CHECK(sasl_client_new("test", "localhost", NULL, NULL, NULL, 0,
                                &native) == SASL_OK);
          status =
              sasl_client_start(native, "CPKT-INTERACTION", &native_prompts,
                                &output, &native_length, &mechanism);
        } else {
          client = cpkt_sasl_client_new("test", "localhost", NULL, NULL, NULL,
                                        0, &status);
          CHECK(client != NULL && status == CPKT_SASL_OK);
          status = client->start(client, "CPKT-INTERACTION", &public_prompts,
                                 &output, &length, &mechanism);
        }
        if (size_case == 3) {
          CHECK(status == CPKT_SASL_BADPARAM);
          if (native_api)
            sasl_dispose(&native);
          else
            client->close(client);
          free(interaction_list);
          continue;
        }
        CHECK(status == CPKT_SASL_INTERACT);
        for (i = 0; i <= interaction_count; ++i) {
          if (native_api) {
            CHECK(native_prompts != NULL);
            if (i == interaction_count) {
              CHECK(native_prompts[i].id == SASL_CB_LIST_END);
              CHECK(native_prompts[i].challenge == NULL);
              CHECK(native_prompts[i].prompt == NULL);
              CHECK(native_prompts[i].defresult == NULL);
              CHECK(native_prompts[i].result == NULL);
              CHECK(native_prompts[i].len == 0);
            } else {
              CHECK(native_prompts[i].id == SASL_CB_AUTHNAME);
              CHECK(strcmp(native_prompts[i].challenge, "challenge") == 0);
              CHECK(strcmp(native_prompts[i].prompt, "prompt") == 0);
              CHECK(strcmp(native_prompts[i].defresult, "default") == 0);
              CHECK(native_prompts[i].len == 7);
              CHECK(memcmp(native_prompts[i].result, "initial", 7) == 0);
              native_prompts[i].result = "answer";
              native_prompts[i].len = 6;
            }
          } else {
            CHECK(public_prompts != NULL);
            if (i == interaction_count) {
              CHECK(public_prompts[i].id == CPKT_SASL_CALLBACK_LIST_END);
              CHECK(public_prompts[i].challenge == NULL);
              CHECK(public_prompts[i].prompt == NULL);
              CHECK(public_prompts[i].default_result == NULL);
              CHECK(public_prompts[i].result == NULL);
              CHECK(public_prompts[i].result_byte_count == 0);
            } else {
              CHECK(public_prompts[i].id ==
                    CPKT_SASL_CALLBACK_AUTHENTICATION_NAME);
              CHECK(strcmp(public_prompts[i].challenge, "challenge") == 0);
              CHECK(strcmp(public_prompts[i].prompt, "prompt") == 0);
              CHECK(strcmp(public_prompts[i].default_result, "default") == 0);
              CHECK(public_prompts[i].result_byte_count == 7);
              CHECK(memcmp(public_prompts[i].result, "initial", 7) == 0);
              public_prompts[i].result = "answer";
              public_prompts[i].result_byte_count = 6;
            }
          }
        }
        if (native_api) {
          CHECK(sasl_client_start(native, "CPKT-INTERACTION", &native_prompts,
                                  &output, &native_length,
                                  &mechanism) == SASL_OK);
          CHECK(native_prompts == NULL && native_length == 5);
          CHECK(output != NULL && memcmp(output, "token", 5) == 0);
          sasl_dispose(&native);
        } else {
          CHECK(client->start(client, "CPKT-INTERACTION", &public_prompts,
                              &output, &length, &mechanism) == CPKT_SASL_OK);
          CHECK(public_prompts == NULL && length == 5);
          CHECK(output != NULL && memcmp(output, "token", 5) == 0);
          client->close(client);
        }
        CHECK(interaction_phase == 2);
        if (mode != 2)
          CHECK(memcmp(&saved, &interaction_list[interaction_count],
                       sizeof(saved)) == 0);
        free(interaction_list);
      }
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
  if (strcmp(argv[1], "interaction_terminator") == 0)
    return interaction_terminator();
  if (strcmp(argv[1], "canonicalization_lengths") == 0)
    return canonicalization_lengths();
  return 1;
}
