#include <cpkt/sasl_plugin.h>

#include <stdio.h>
#include <string.h>

static int client_new_calls;
static int server_new_calls;
static int client_step_calls;
static int server_step_calls;
static int client_dispose_calls;
static int server_dispose_calls;
static int client_free_calls;
static int server_free_calls;
static int client_info_calls;
static int server_info_calls;
static int global_log_calls;
static int local_auth_calls;
static int canonical_client_calls;
static int canonical_server_calls;
static int canonical_free_calls;
static int auxiliary_store_calls;
static int auxiliary_lookup_calls;
static int auxiliary_free_calls;
static int auxiliary_info_calls;
static const cpkt_sasl_plugin_utils *retained_client_init_utils;
static const cpkt_sasl_plugin_utils *retained_server_init_utils;
static const cpkt_sasl_plugin_utils *retained_client_connection_utils;
static const cpkt_sasl_plugin_utils *first_client_connection_utils;
static const cpkt_sasl_plugin_utils *retained_server_connection_utils;
static int retained_utility_calls;
static int layer_encode_calls;
static int layer_decode_calls;

static int layer_encode(cpkt_sasl_plugin_output *self,
                        const cpkt_sasl_iov *vectors, size_t count,
                        const char **output, unsigned long *length_out) {
  if (self->encode_context != &client_step_calls || count != 1 ||
      vectors == 0 || vectors[0].byte_count != 3 ||
      memcmp(vectors[0].data, "abc", 3) != 0)
    return CPKT_SASL_BADPARAM;
  ++layer_encode_calls;
  *output = "sealed";
  *length_out = 6;
  return CPKT_SASL_OK;
}

static int layer_decode(cpkt_sasl_plugin_output *self, const char *input,
                        unsigned long input_length, const char **output,
                        unsigned long *length_out) {
  if (self->decode_context != &client_step_calls || input_length != 6 ||
      memcmp(input, "sealed", 6) != 0)
    return CPKT_SASL_BADPARAM;
  ++layer_decode_calls;
  *output = "opened";
  *length_out = 6;
  return CPKT_SASL_OK;
}

static int retained_utility_works(const cpkt_sasl_plugin_utils *utils) {
  void *value;
  if (utils == 0 || utils->allocate == 0 || utils->release == 0)
    return 0;
  value = utils->allocate(utils, 16);
  if (value == 0)
    return 0;
  utils->release(utils, value);
  ++retained_utility_calls;
  return 1;
}

static int local_auth(void *context, int id, const char **result,
                      unsigned long *length) {
  if (context != &local_auth_calls ||
      id != CPKT_SASL_CALLBACK_AUTHENTICATION_NAME || result == 0)
    return CPKT_SASL_BADPARAM;
  ++local_auth_calls;
  *result = "local-user";
  if (length != 0)
    *length = 10;
  return CPKT_SASL_OK;
}

static int plugin_option(void *context, const char *plugin_name,
                         const char *option, const char **result,
                         unsigned long *length) {
  (void)plugin_name;
  if (context != &global_log_calls || option == 0 || result == 0)
    return CPKT_SASL_BADPARAM;
  if (strcmp(option, "canon_user_plugin") == 0)
    *result = "CPKT-CANON";
  else if (strcmp(option, "auxprop_plugin") == 0)
    *result = "CPKT-AUX";
  else
    return CPKT_SASL_FAIL;
  if (length != 0)
    *length = (unsigned long)strlen(*result);
  return CPKT_SASL_OK;
}

static int canonical_client(void *context, cpkt_sasl_client_params *params,
                            const char *user, unsigned long length,
                            unsigned long flags, char *output,
                            unsigned long capacity,
                            unsigned long *output_length) {
  (void)flags;
  if (user != 0 && length == 0)
    length = (unsigned long)strlen(user);
  if (context != &canonical_client_calls || params == 0 || user == 0 ||
      length != 9 || capacity < 11 || memcmp(user, "test-user", 9) != 0)
    return CPKT_SASL_BADPARAM;
  memcpy(output, "canon-user", 11);
  *output_length = 10;
  ++canonical_client_calls;
  return CPKT_SASL_OK;
}

static int canonical_server(void *context, cpkt_sasl_server_params *params,
                            const char *user, unsigned long length,
                            unsigned long flags, char *output,
                            unsigned long capacity,
                            unsigned long *output_length) {
  (void)flags;
  if (user != 0 && length == 0)
    length = (unsigned long)strlen(user);
  if (context != &canonical_client_calls || params == 0 || user == 0 ||
      capacity < 11 ||
      !((length == 9 && memcmp(user, "test-user", 9) == 0) ||
        (length == 10 && memcmp(user, "canon-user", 10) == 0)))
    return CPKT_SASL_BADPARAM;
  memcpy(output, "canon-user", 11);
  *output_length = 10;
  ++canonical_server_calls;
  return CPKT_SASL_OK;
}

static void canonical_free(void *context, const cpkt_sasl_plugin_utils *utils) {
  if (context == &canonical_client_calls && utils != 0)
    ++canonical_free_calls;
}

static int canonical_init(void *context, const cpkt_sasl_plugin_utils *utils,
                          int maximum_version, int *version_out,
                          const cpkt_sasl_canonicalizer_plugin **plugin_out,
                          const char *name) {
  static cpkt_sasl_canonicalizer_plugin plugin;
  if (context != &canonical_client_calls || utils == 0 ||
      maximum_version < CPKT_SASL_CANONICALIZER_PLUGIN_VERSION ||
      strcmp(name, "CPKT-CANON") != 0)
    return CPKT_SASL_FAIL;
  memset(&plugin, 0, sizeof(plugin));
  plugin.context = context;
  plugin.name = "CPKT-CANON";
  plugin.client = canonical_client;
  plugin.server = canonical_server;
  plugin.free_context = canonical_free;
  *version_out = CPKT_SASL_CANONICALIZER_PLUGIN_VERSION;
  *plugin_out = &plugin;
  return CPKT_SASL_OK;
}

static int auxiliary_lookup(void *context, cpkt_sasl_server_params *params,
                            unsigned long flags, const char *user,
                            unsigned long length) {
  (void)flags;
  if (context != &auxiliary_store_calls || params == 0 || user == 0 ||
      length != 10 || memcmp(user, "canon-user", 10) != 0 ||
      params->properties == 0 ||
      cpkt_sasl_property_set(params->properties, CPKT_SASL_AUX_PASSWORD,
                             "secret", 6) != CPKT_SASL_OK)
    return CPKT_SASL_BADPARAM;
  ++auxiliary_lookup_calls;
  return CPKT_SASL_OK;
}

static int auxiliary_store(void *context, cpkt_sasl_server_params *params,
                           cpkt_sasl_property_context *properties,
                           const char *user, unsigned long length) {
  const cpkt_sasl_property_value *values;
  size_t count = 0;
  if (context != &auxiliary_store_calls || params == 0 || properties == 0 ||
      user == 0 || length != 9 || memcmp(user, "test-user", 9) != 0)
    return CPKT_SASL_BADPARAM;
  values = cpkt_sasl_property_get(properties, &count);
  if (values == 0 || count != 1 || values[0].value_count != 1 ||
      strcmp(values[0].values[0], "value") != 0)
    return CPKT_SASL_FAIL;
  ++auxiliary_store_calls;
  return CPKT_SASL_OK;
}

static void auxiliary_free(void *context, const cpkt_sasl_plugin_utils *utils) {
  if (context == &auxiliary_store_calls && utils != 0)
    ++auxiliary_free_calls;
}

static int auxiliary_init(void *context, const cpkt_sasl_plugin_utils *utils,
                          int maximum_version, int *version_out,
                          const cpkt_sasl_auxiliary_plugin **plugin_out,
                          const char *name) {
  static cpkt_sasl_auxiliary_plugin plugin;
  if (context != &auxiliary_store_calls || utils == 0 ||
      maximum_version < CPKT_SASL_AUXILIARY_PLUGIN_VERSION ||
      strcmp(name, "CPKT-AUX") != 0)
    return CPKT_SASL_FAIL;
  memset(&plugin, 0, sizeof(plugin));
  plugin.context = context;
  plugin.name = "CPKT-AUX";
  plugin.lookup = auxiliary_lookup;
  plugin.store = auxiliary_store;
  plugin.free_context = auxiliary_free;
  *version_out = CPKT_SASL_AUXILIARY_PLUGIN_VERSION;
  *plugin_out = &plugin;
  return CPKT_SASL_OK;
}

static int global_log(void *context, int level, const char *message) {
  (void)level;
  (void)message;
  if (context == &global_log_calls)
    ++global_log_calls;
  return CPKT_SASL_OK;
}

static int test_plugin_utilities(const cpkt_sasl_plugin_utils *utils) {
  const char *names[2] = {"contractKey", 0};
  const cpkt_sasl_property_value *values;
  cpkt_sasl_property_context *properties = 0, *copy = 0;
  unsigned char digest[16];
  char encoded[16], decoded[16];
  unsigned long length = 0;
  size_t count = 0;
  void *allocated, *zeroed, *mutex;
  int status = 0;
  if (utils->allocate == 0 || utils->allocate_zeroed == 0 ||
      utils->resize == 0 || utils->release == 0 || utils->mutex_new == 0 ||
      utils->mutex_lock == 0 || utils->mutex_unlock == 0 ||
      utils->mutex_free == 0 || utils->property_new == 0 ||
      utils->property_request == 0 || utils->property_set == 0 ||
      utils->property_get == 0 || utils->property_duplicate == 0 ||
      utils->property_dispose == 0 || utils->base64_encode == 0 ||
      utils->base64_decode == 0 || utils->utf8_verify == 0 ||
      utils->hmac_md5 == 0 || utils->log == 0)
    return 1;
  allocated = utils->allocate(utils, 4);
  zeroed = utils->allocate_zeroed(utils, 4, 1);
  if (allocated == 0 || zeroed == 0 || ((char *)zeroed)[0] != 0)
    return 2;
  allocated = utils->resize(utils, allocated, 8);
  if (allocated == 0)
    return 3;
  utils->release(utils, allocated);
  utils->release(utils, zeroed);
  mutex = utils->mutex_new(utils);
  if (mutex == 0 || utils->mutex_lock(utils, mutex) != 0 ||
      utils->mutex_unlock(utils, mutex) != 0)
    return 4;
  utils->mutex_free(utils, mutex);
  if (utils->utf8_verify(utils, "abc", 3) != CPKT_SASL_OK ||
      utils->base64_encode(utils, "abc", 3, encoded, sizeof(encoded),
                           &length) != CPKT_SASL_OK ||
      length != 4 || memcmp(encoded, "YWJj", 4) != 0 ||
      utils->base64_decode(utils, encoded, length, decoded, sizeof(decoded),
                           &length) != CPKT_SASL_OK ||
      length != 3 || memcmp(decoded, "abc", 3) != 0)
    return 5;
  utils->hmac_md5(utils, (const unsigned char *)"abc", 3,
                  (const unsigned char *)"key", 3, digest);
  if (digest[0] != 0xd2 || digest[1] != 0xfe)
    return 6;
  properties = utils->property_new(utils, 4);
  if (properties == 0 ||
      utils->property_request(utils, properties, names) != CPKT_SASL_OK ||
      utils->property_set(utils, properties, "contractKey", "value", 5) !=
          CPKT_SASL_OK)
    status = 7;
  if (status == 0) {
    values = utils->property_get(utils, properties, &count);
    if (values == 0 || count != 1 || values[0].value_count != 1 ||
        strcmp(values[0].values[0], "value") != 0 ||
        utils->property_duplicate(utils, properties, &copy) != CPKT_SASL_OK ||
        copy == 0)
      status = 8;
  }
  if (copy != 0)
    utils->property_dispose(utils, &copy);
  if (properties != 0)
    utils->property_dispose(utils, &properties);
  return status;
}

static int client_new(void *context, cpkt_sasl_client_params *params,
                      void **connection_out) {
  const cpkt_sasl_callbacks *callbacks = 0;
  const char *selected_user = 0;
  const char *selected_path = 0;
  const char *selected_option = 0;
  unsigned long selected_option_length = 0;
  const char *external_identity = 0;
  cpkt_sasl_security_properties selected_security;
  cpkt_sasl_http_request selected_request;
  unsigned long selected_length = 0;
  if (context != &client_new_calls || params == 0 || params->utils == 0 ||
      params->utils->connection == 0 || params->prompt_callbacks == 0 ||
      params->prompt_callbacks->simple != local_auth ||
      params->utils->version < CPKT_SASL_UTILS_VERSION) {
    fprintf(stderr,
            "new args: context=%p expected=%p params=%p utils=%p version=%d "
            "prompts=%p\n",
            context, (void *)&client_new_calls, (void *)params,
            params == 0 ? 0 : (void *)params->utils,
            params == 0 || params->utils == 0 ? -1 : params->utils->version,
            params == 0 ? 0 : (void *)params->prompt_callbacks);
    return CPKT_SASL_FAIL;
  }
  params->utils->log(params->utils, CPKT_SASL_LOG_ERROR,
                     "cpkt-plugin-log-marker");
  if (params->utils->get_text_property(
          params->utils, CPKT_SASL_PROPERTY_EXTERNAL_AUTHENTICATION,
          &external_identity) != CPKT_SASL_OK ||
      external_identity == 0 || strcmp(external_identity, "plugin-user") != 0 ||
      params->utils->connection->get_security_properties(
          params->utils->connection, &selected_security) != CPKT_SASL_OK ||
      selected_security.maximum_buffer_bytes != 1024 ||
      params->utils->connection->get_http_request(
          params->utils->connection, &selected_request) != CPKT_SASL_OK ||
      selected_request.method == 0 ||
      strcmp(selected_request.method, "GET") != 0) {
    fprintf(stderr,
            "client property mismatch: external=%s security=%lu http=%s\n",
            external_identity == 0 ? "(null)" : external_identity,
            selected_security.maximum_buffer_bytes,
            selected_request.method == 0 ? "(null)" : selected_request.method);
    return CPKT_SASL_FAIL;
  }
  {
    int callback_status =
        params->utils->get_callbacks == 0
            ? CPKT_SASL_FAIL
            : params->utils->get_callbacks(params->utils,
                                           CPKT_SASL_CALLBACK_LOG, &callbacks);
    if (callback_status != CPKT_SASL_OK || callbacks == 0 ||
        callbacks->context != &global_log_calls ||
        callbacks->log != global_log) {
      fprintf(stderr, "new callback: status=%d record=%p context=%p\n",
              callback_status, (void *)callbacks,
              callbacks == 0 ? 0 : callbacks->context);
      return CPKT_SASL_FAIL;
    }
    if (callbacks->log(callbacks->context, CPKT_SASL_LOG_ERROR,
                       "selected-global-log") != CPKT_SASL_OK)
      return CPKT_SASL_FAIL;
    callback_status = params->utils->get_callbacks(
        params->utils, CPKT_SASL_CALLBACK_AUTHENTICATION_NAME, &callbacks);
    if (callback_status != CPKT_SASL_OK || callbacks == 0 ||
        callbacks->context != &local_auth_calls ||
        callbacks->simple != local_auth ||
        callbacks->simple(callbacks->context,
                          CPKT_SASL_CALLBACK_AUTHENTICATION_NAME,
                          &selected_user, &selected_length) != CPKT_SASL_OK ||
        selected_length != 10 || strcmp(selected_user, "local-user") != 0 ||
        local_auth_calls != client_new_calls + 1)
      return CPKT_SASL_FAIL;
    callback_status = params->utils->get_callbacks(
        params->utils, CPKT_SASL_CALLBACK_REALM, &callbacks);
    if (callback_status != CPKT_SASL_FAIL || callbacks != 0)
      return CPKT_SASL_FAIL;
    callback_status = params->utils->get_callbacks(
        params->utils, CPKT_SASL_CALLBACK_PLUGIN_PATH, &callbacks);
    if (callback_status != CPKT_SASL_OK || callbacks == 0 ||
        callbacks->plugin_path == 0 ||
        callbacks->context == &global_log_calls ||
        callbacks->context == &local_auth_calls ||
        callbacks->plugin_path(callbacks->context, &selected_path) !=
            CPKT_SASL_OK ||
        selected_path == 0 ||
        strcmp(selected_path, "/cpkt-no-external-sasl-plugins") != 0)
      return CPKT_SASL_FAIL;
    callback_status = params->utils->get_callbacks(
        params->utils, CPKT_SASL_CALLBACK_PASSWORD, &callbacks);
    if (callback_status != CPKT_SASL_FAIL || callbacks != 0)
      return CPKT_SASL_FAIL;
    callback_status = params->utils->get_callbacks(
        params->utils, CPKT_SASL_CALLBACK_OPTION, &callbacks);
    if (callback_status != CPKT_SASL_OK || callbacks == 0 ||
        callbacks->context == &global_log_calls ||
        callbacks->context == &local_auth_calls || callbacks->option == 0 ||
        callbacks->option(callbacks->context, "CPKT-TEST", "auxprop_plugin",
                          &selected_option,
                          &selected_option_length) != CPKT_SASL_OK ||
        selected_option == 0 || selected_option_length != 8 ||
        strcmp(selected_option, "CPKT-AUX") != 0)
      return CPKT_SASL_FAIL;
  }
  ++client_new_calls;
  retained_client_connection_utils = params->utils;
  if (first_client_connection_utils == 0)
    first_client_connection_utils = params->utils;
  else if (first_client_connection_utils == params->utils)
    return CPKT_SASL_FAIL;
  *connection_out = context;
  return CPKT_SASL_OK;
}

static int client_step(void *context, cpkt_sasl_client_params *params,
                       const char *input, unsigned long input_length,
                       cpkt_sasl_interaction **interaction_out,
                       const char **output, unsigned long *length_out,
                       cpkt_sasl_plugin_output *out) {
  (void)interaction_out;
  if (context != &client_new_calls || params == 0 ||
      (input != 0 && input_length != 0) ||
      params->utils != retained_client_connection_utils ||
      !retained_utility_works(retained_client_init_utils) ||
      !retained_utility_works(retained_client_connection_utils))
    return CPKT_SASL_FAIL;
  out->encode_context = &client_step_calls;
  out->decode_context = &client_step_calls;
  out->encode = layer_encode;
  out->decode = layer_decode;
  out->client_credentials = &client_step_calls;
  if (params->canonicalize == 0 ||
      params->canonicalize(params, "test-user", 9,
                           CPKT_SASL_CANONICALIZE_AUTHENTICATION_ID |
                               CPKT_SASL_CANONICALIZE_AUTHORIZATION_ID,
                           out) != CPKT_SASL_OK ||
      out->encode != layer_encode || out->decode != layer_decode ||
      out->encode_context != &client_step_calls ||
      out->decode_context != &client_step_calls ||
      out->client_credentials != &client_step_calls)
    return CPKT_SASL_FAIL;
  ++client_step_calls;
  *output = "hello";
  *length_out = 5;
  out->done = 1;
  out->user = "test-user";
  out->authentication_identity = "test-user";
  out->user_length = 9;
  out->authentication_length = 9;
  out->maximum_output_bytes = 64;
  out->mechanism_ssf = 1;
  return CPKT_SASL_OK;
}

static void client_dispose(void *context, const cpkt_sasl_plugin_utils *utils) {
  if (context == &client_new_calls &&
      (utils == first_client_connection_utils ||
       utils == retained_client_connection_utils) &&
      retained_utility_works(utils))
    ++client_dispose_calls;
}

static void client_free(void *context, const cpkt_sasl_plugin_utils *utils) {
  if (context == &client_new_calls && utils == retained_client_init_utils &&
      retained_utility_works(utils))
    ++client_free_calls;
}

static int server_new(void *context, cpkt_sasl_server_params *params,
                      const char *challenge, unsigned long challenge_length,
                      void **connection_out) {
  (void)challenge;
  (void)challenge_length;
  if (context != &server_new_calls || params == 0 || params->utils == 0 ||
      params->callbacks == 0 || params->callbacks->log != global_log)
    return CPKT_SASL_FAIL;
  ++server_new_calls;
  retained_server_connection_utils = params->utils;
  *connection_out = context;
  return CPKT_SASL_OK;
}

static int server_step(void *context, cpkt_sasl_server_params *params,
                       const char *input, unsigned long input_length,
                       const char **output, unsigned long *length_out,
                       cpkt_sasl_plugin_output *out) {
  if (context != &server_new_calls || params == 0 || input_length != 5 ||
      memcmp(input, "hello", 5) != 0 ||
      params->utils != retained_server_connection_utils ||
      !retained_utility_works(retained_server_init_utils) ||
      !retained_utility_works(retained_server_connection_utils))
    return CPKT_SASL_FAIL;
  if (params->canonicalize == 0 ||
      params->canonicalize(params, "test-user", 9,
                           CPKT_SASL_CANONICALIZE_AUTHENTICATION_ID |
                               CPKT_SASL_CANONICALIZE_AUTHORIZATION_ID,
                           out) != CPKT_SASL_OK)
    return CPKT_SASL_FAIL;
  ++server_step_calls;
  *output = "world";
  *length_out = 5;
  out->done = 1;
  out->user = "test-user";
  out->authentication_identity = "test-user";
  out->user_length = 9;
  out->authentication_length = 9;
  return CPKT_SASL_OK;
}

static void server_dispose(void *context, const cpkt_sasl_plugin_utils *utils) {
  if (context == &server_new_calls &&
      utils == retained_server_connection_utils &&
      retained_utility_works(utils))
    ++server_dispose_calls;
}

static void server_free(void *context, const cpkt_sasl_plugin_utils *utils) {
  if (context == &server_new_calls && utils == retained_server_init_utils &&
      retained_utility_works(utils))
    ++server_free_calls;
}

static int client_init(void *context, const cpkt_sasl_plugin_utils *utils,
                       int maximum_version, int *version_out,
                       const cpkt_sasl_client_plugin **plugins_out,
                       int *count_out) {
  static cpkt_sasl_client_plugin plugin;
  static const unsigned long prompts[] = {CPKT_SASL_CALLBACK_LIST_END};
  cpkt_sasl_md5_context md5;
  unsigned char digest[16];
  if (context != &client_new_calls || utils == 0 ||
      maximum_version < CPKT_SASL_CLIENT_PLUGIN_VERSION ||
      utils->option_context != &global_log_calls || utils->md5_init == 0 ||
      utils->md5_update == 0 || utils->md5_final == 0)
    return CPKT_SASL_FAIL;
  retained_client_init_utils = utils;
  utils->md5_init(utils, &md5);
  if (utils->md5_update(utils, &md5, (const unsigned char *)"abc", 3) !=
      CPKT_SASL_OK)
    return CPKT_SASL_FAIL;
  utils->md5_final(utils, digest, &md5);
  if (digest[0] != 0x90 || digest[1] != 0x01)
    return CPKT_SASL_FAIL;
  {
    int utility_status = test_plugin_utilities(utils);
    if (utility_status != 0) {
      fprintf(stderr, "utility slot status=%d\n", utility_status);
      return CPKT_SASL_FAIL;
    }
  }
  memset(&plugin, 0, sizeof(plugin));
  plugin.mechanism_name = "CPKT-TEST";
  plugin.maximum_ssf = 1;
  plugin.security_flags = CPKT_SASL_SECURITY_NO_ANONYMOUS;
  plugin.required_prompts = prompts;
  plugin.context = context;
  plugin.new_connection = client_new;
  plugin.step = client_step;
  plugin.dispose_connection = client_dispose;
  plugin.free_context = client_free;
  *version_out = CPKT_SASL_CLIENT_PLUGIN_VERSION;
  *plugins_out = &plugin;
  *count_out = 1;
  return CPKT_SASL_OK;
}

static int server_init(void *context, const cpkt_sasl_plugin_utils *utils,
                       int maximum_version, int *version_out,
                       const cpkt_sasl_server_plugin **plugins_out,
                       int *count_out) {
  static cpkt_sasl_server_plugin plugin;
  if (context != &server_new_calls || utils == 0 ||
      maximum_version < CPKT_SASL_SERVER_PLUGIN_VERSION)
    return CPKT_SASL_FAIL;
  retained_server_init_utils = utils;
  memset(&plugin, 0, sizeof(plugin));
  plugin.mechanism_name = "CPKT-TEST";
  plugin.security_flags = CPKT_SASL_SECURITY_NO_ANONYMOUS;
  plugin.context = context;
  plugin.new_connection = server_new;
  plugin.step = server_step;
  plugin.dispose_connection = server_dispose;
  plugin.free_context = server_free;
  *version_out = CPKT_SASL_SERVER_PLUGIN_VERSION;
  *plugins_out = &plugin;
  *count_out = 1;
  return CPKT_SASL_OK;
}

static void client_info(void *context, const cpkt_sasl_plugin_info *info,
                        int stage) {
  if (context == &client_info_calls && stage == CPKT_SASL_INFO_LIST_MECHANISM &&
      info != 0 && info->mechanism_name != 0 &&
      strcmp(info->mechanism_name, "CPKT-TEST") == 0 &&
      info->client_plugin != 0 &&
      info->client_plugin->context == &client_new_calls &&
      info->client_plugin->step == client_step)
    ++client_info_calls;
}

static void server_info(void *context, const cpkt_sasl_plugin_info *info,
                        int stage) {
  if (context == &server_info_calls && stage == CPKT_SASL_INFO_LIST_MECHANISM &&
      info != 0 && info->mechanism_name != 0 &&
      strcmp(info->mechanism_name, "CPKT-TEST") == 0 &&
      info->server_plugin != 0 &&
      info->server_plugin->context == &server_new_calls &&
      info->server_plugin->step == server_step)
    ++server_info_calls;
}

static void auxiliary_info(void *context, const cpkt_sasl_plugin_info *info,
                           int stage) {
  if (context == &auxiliary_info_calls &&
      stage == CPKT_SASL_INFO_LIST_MECHANISM && info != 0 &&
      info->mechanism_name != 0 &&
      strcmp(info->mechanism_name, "CPKT-AUX") == 0 &&
      info->auxiliary_plugin != 0 &&
      info->auxiliary_plugin->context == &auxiliary_store_calls)
    ++auxiliary_info_calls;
}

int main(void) {
  cpkt_sasl_callbacks callbacks;
  cpkt_sasl_callbacks local_callbacks;
  cpkt_sasl_security_properties security;
  cpkt_sasl_http_request request;
  cpkt_sasl_property_context *properties;
  const char *property_names[2] = {"contractKey", 0};
  cpkt_sasl_md5_context md5;
  cpkt_sasl_hmac_md5_context hmac;
  cpkt_sasl_hmac_md5_state hmac_state;
  unsigned char digest[16];
  cpkt_sasl *client = 0, *second_client = 0, *server = 0;
  cpkt_sasl_interaction *interactions = 0;
  cpkt_sasl_iov layer_input;
  const char *client_output = 0, *server_output = 0, *mechanism = 0;
  const char *initial_token = 0;
  const void *delegated_payload = 0;
  cpkt_gss_credential *delegated_gss = 0;
  unsigned long client_length = 0, server_length = 0;
  int status = 0;
  cpkt_sasl_md5_initialize(&md5);
  if (cpkt_sasl_md5_update(&md5, (const unsigned char *)"abc", 3) !=
      CPKT_SASL_OK)
    return 10;
  cpkt_sasl_md5_final(digest, &md5);
  if (digest[0] != 0x90 || digest[1] != 0x01)
    return 11;
  cpkt_sasl_hmac_md5_precalculate(&hmac_state, (const unsigned char *)"key", 3);
  cpkt_sasl_hmac_md5_import(&hmac, &hmac_state);
  if (cpkt_sasl_hmac_md5_update(&hmac, (const unsigned char *)"abc", 3) !=
      CPKT_SASL_OK)
    return 12;
  cpkt_sasl_hmac_md5_final(digest, &hmac);
  if (digest[0] != 0xd2 || digest[1] != 0xfe)
    return 13;
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.context = &global_log_calls;
  callbacks.log = global_log;
  callbacks.option = plugin_option;
  memset(&local_callbacks, 0, sizeof(local_callbacks));
  local_callbacks.context = &local_auth_calls;
  local_callbacks.simple = local_auth;
  if (cpkt_sasl_set_path(CPKT_SASL_PATH_PLUGIN,
                         "/cpkt-no-external-sasl-plugins") != CPKT_SASL_OK ||
      cpkt_sasl_client_initialize(&callbacks) != CPKT_SASL_OK ||
      cpkt_sasl_server_initialize(0, "cpkt-plugin-test") != CPKT_SASL_OK)
    return 1;
  if (cpkt_sasl_canonicalizer_add_plugin("CPKT-CANON", canonical_init,
                                         &canonical_client_calls) !=
          CPKT_SASL_OK ||
      cpkt_sasl_auxiliary_add_plugin("CPKT-AUX", auxiliary_init,
                                     &auxiliary_store_calls) != CPKT_SASL_OK)
    return 14;
  status =
      cpkt_sasl_client_add_plugin("cpkt-test", client_init, &client_new_calls);
  if (status != CPKT_SASL_OK) {
    fprintf(stderr, "client registration: %d\n", status);
    return 2;
  }
  status =
      cpkt_sasl_server_add_plugin("cpkt-test", server_init, &server_new_calls);
  if (status != CPKT_SASL_OK) {
    fprintf(stderr, "server registration: %d\n", status);
    return 2;
  }
  if (cpkt_sasl_client_plugin_info("CPKT-TEST", client_info,
                                   &client_info_calls) != CPKT_SASL_OK ||
      cpkt_sasl_server_plugin_info("CPKT-TEST", server_info,
                                   &server_info_calls) != CPKT_SASL_OK ||
      cpkt_sasl_auxiliary_plugin_info("CPKT-AUX", auxiliary_info,
                                      &auxiliary_info_calls) != CPKT_SASL_OK ||
      client_info_calls != 1 || server_info_calls != 1 ||
      auxiliary_info_calls != 1)
    return 3;
  client = cpkt_sasl_client_new("test", "localhost", 0, 0, &local_callbacks, 0,
                                &status);
  if (client == 0 || status != CPKT_SASL_OK)
    return 4;
  memset(&security, 0, sizeof(security));
  security.maximum_ssf = 1;
  security.maximum_buffer_bytes = 1024;
  if (client->set_security_properties(client, &security) != CPKT_SASL_OK)
    return 4;
  memset(&request, 0, sizeof(request));
  request.method = "GET";
  request.uri = "/plugin";
  if (client->set_http_request(client, &request) != CPKT_SASL_OK ||
      client->set_text_property(client,
                                CPKT_SASL_PROPERTY_EXTERNAL_AUTHENTICATION,
                                "plugin-user") != CPKT_SASL_OK)
    return 4;
  server = cpkt_sasl_server_new("test", "localhost", 0, 0, 0, &callbacks, 0,
                                &status);
  if (server == 0 || status != CPKT_SASL_OK)
    return 5;
  properties = cpkt_sasl_property_new(4);
  if (properties == 0 ||
      cpkt_sasl_property_request(properties, property_names) != CPKT_SASL_OK ||
      cpkt_sasl_property_set(properties, "contractKey", "value", 5) !=
          CPKT_SASL_OK ||
      server->auxiliary_store(server, properties, "test-user") !=
          CPKT_SASL_OK ||
      auxiliary_store_calls != 1)
    return 15;
  cpkt_sasl_property_dispose(&properties);
  status = server->check_password(server, "test-user", 9, "secret", 6);
  if (status != CPKT_SASL_OK || auxiliary_lookup_calls == 0) {
    fprintf(stderr, "aux lookup: status=%d calls=%d canon=%d detail=%s\n",
            status, auxiliary_lookup_calls, canonical_server_calls,
            server->error_detail(server));
    return 16;
  }
  status = client->start(client, "CPKT-TEST", &interactions, &client_output,
                         &client_length, &mechanism);
  if (status != CPKT_SASL_OK || client_length != 5 || client_output == 0 ||
      memcmp(client_output, "hello", 5) != 0 || global_log_calls == 0) {
    fprintf(stderr, "client start: status=%d len=%lu detail=%s calls=%d\n",
            status, client_length, client->error_detail(client),
            client_step_calls);
    return 6;
  }
  initial_token = client_output;
  if (client->get_delegated_payload(client, &delegated_payload) !=
          CPKT_SASL_OK ||
      delegated_payload != &client_step_calls ||
      client->get_delegated_credentials(client, &delegated_gss) !=
          CPKT_SASL_BADPROT ||
      delegated_gss != 0)
    return 21;
  layer_input.data = "abc";
  layer_input.byte_count = 3;
  if (client->encode_vector(client, &layer_input, 1, &client_output,
                            &client_length) != CPKT_SASL_OK ||
      client_length != 6 || memcmp(client_output, "sealed", 6) != 0 ||
      client->decode(client, "sealed", 6, &client_output, &client_length) !=
          CPKT_SASL_OK ||
      client_length != 6 || memcmp(client_output, "opened", 6) != 0 ||
      layer_encode_calls != 1 || layer_decode_calls != 1) {
    fprintf(stderr, "layer: len=%lu encode=%d decode=%d detail=%s\n",
            client_length, layer_encode_calls, layer_decode_calls,
            client->error_detail(client));
    return 18;
  }
  second_client = cpkt_sasl_client_new("test", "localhost", 0, 0,
                                       &local_callbacks, 0, &status);
  if (second_client == 0 || status != CPKT_SASL_OK ||
      second_client->set_security_properties(second_client, &security) !=
          CPKT_SASL_OK ||
      second_client->set_http_request(second_client, &request) !=
          CPKT_SASL_OK ||
      second_client->set_text_property(
          second_client, CPKT_SASL_PROPERTY_EXTERNAL_AUTHENTICATION,
          "plugin-user") != CPKT_SASL_OK ||
      second_client->start(second_client, "CPKT-TEST", &interactions,
                           &client_output, &client_length,
                           &mechanism) != CPKT_SASL_OK ||
      retained_client_connection_utils == first_client_connection_utils)
    return 19;
  second_client->close(second_client);
  if (client_dispose_calls != 1 ||
      !retained_utility_works(retained_client_init_utils))
    return 20;
  status = server->server_start(server, "CPKT-TEST", initial_token, 5,
                                &server_output, &server_length);
  if (status != CPKT_SASL_CONTINUE || server_length != 5 ||
      server_output == 0 || memcmp(server_output, "world", 5) != 0) {
    fprintf(stderr, "server start: status=%d len=%lu detail=%s calls=%d\n",
            status, server_length, server->error_detail(server),
            server_step_calls);
    return 7;
  }
  if (server->step(server, 0, 0, 0, &server_output, &server_length) !=
      CPKT_SASL_OK)
    return 9;
  client->close(client);
  server->close(server);
  if (cpkt_sasl_client_finish() != CPKT_SASL_OK ||
      cpkt_sasl_server_finish() != CPKT_SASL_OK || client_new_calls != 2 ||
      server_new_calls != 1 || client_step_calls != 2 ||
      server_step_calls != 1 || client_dispose_calls != 2 ||
      server_dispose_calls != 1 || client_free_calls != 1 ||
      server_free_calls != 1 || canonical_client_calls == 0 ||
      canonical_server_calls == 0 || canonical_free_calls == 0 ||
      auxiliary_free_calls == 0)
    return 8;
  if (retained_utility_calls < 8)
    return 17;
  return 0;
}
