#include <cpkt/sasl_plugin.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      fprintf(stderr, "idle/registration check failed at %d: %s\n", __LINE__,  \
              #expression);                                                    \
      return 1;                                                                \
    }                                                                          \
  } while (0)

typedef struct shared_context {
  int references;
  int action;
  int condition;
  const cpkt_sasl_plugin_utils *utils;
} shared_context;

static cpkt_sasl *expected_client;
static cpkt_sasl *expected_server;
static int client_global_calls;
static int client_receiver_calls;
static int server_receiver_calls;
static int client_free_calls;
static int server_free_calls;
static int final_free_calls;
static int callback_error;
static int expected_condition;
static int info_calls;

static int quiet_log(void *context, int level, const char *message) {
  (void)context;
  (void)level;
  (void)message;
  return CPKT_SASL_OK;
}

static int utility_is_alive(const cpkt_sasl_plugin_utils *utils) {
  void *memory;
  if (utils == NULL || utils->version != CPKT_SASL_UTILS_VERSION)
    return 0;
  memory = utils->allocate(utils, 8);
  if (memory == NULL)
    return 0;
  utils->release(utils, memory);
  return 1;
}

static void release_shared(shared_context *context,
                           const cpkt_sasl_plugin_utils *utils) {
  if (context->utils != utils || !utility_is_alive(utils) ||
      context->references <= 0)
    callback_error = 1;
  --context->references;
  if (context->references == 0) {
    ++final_free_calls;
    free(context);
  }
}

static void client_free(void *context, const cpkt_sasl_plugin_utils *utils) {
  ++client_free_calls;
  release_shared((shared_context *)context, utils);
}

static void server_free(void *context, const cpkt_sasl_plugin_utils *utils) {
  ++server_free_calls;
  release_shared((shared_context *)context, utils);
}

static int client_idle(void *context, cpkt_sasl *receiver,
                       cpkt_sasl_client_params *params) {
  shared_context *state = (shared_context *)context;
  if (!utility_is_alive(state->utils))
    callback_error = 1;
  if (receiver == NULL) {
    ++client_global_calls;
    if (params != NULL)
      callback_error = 1;
  } else {
    ++client_receiver_calls;
    if (receiver != expected_client || params == NULL ||
        params->utils->connection != receiver ||
        (params->service != NULL && strcmp(params->service, "idle-test") != 0))
      callback_error = 1;
  }
  return state->action;
}

static int server_idle(void *context, cpkt_sasl *receiver,
                       cpkt_sasl_server_params *params) {
  shared_context *state = (shared_context *)context;
  ++server_receiver_calls;
  if (receiver != expected_server || params == NULL ||
      params->utils->connection != receiver ||
      (params->service != NULL && strcmp(params->service, "idle-test") != 0) ||
      !utility_is_alive(state->utils))
    callback_error = 1;
  return state->action;
}

static int client_new(void *context, cpkt_sasl_client_params *params,
                      void **connection_out) {
  (void)context;
  (void)params;
  *connection_out = NULL;
  return CPKT_SASL_FAIL;
}

static int client_step(void *context, cpkt_sasl_client_params *params,
                       const char *input, unsigned long input_length,
                       cpkt_sasl_interaction **interactions,
                       const char **output, unsigned long *output_length,
                       cpkt_sasl_plugin_output *output_params) {
  (void)context;
  (void)params;
  (void)input;
  (void)input_length;
  (void)interactions;
  (void)output;
  (void)output_length;
  (void)output_params;
  return CPKT_SASL_FAIL;
}

static int server_new(void *context, cpkt_sasl_server_params *params,
                      const char *challenge, unsigned long challenge_length,
                      void **connection_out) {
  (void)context;
  (void)params;
  (void)challenge;
  (void)challenge_length;
  *connection_out = NULL;
  return CPKT_SASL_FAIL;
}

static int server_step(void *context, cpkt_sasl_server_params *params,
                       const char *input, unsigned long input_length,
                       const char **output, unsigned long *output_length,
                       cpkt_sasl_plugin_output *output_params) {
  (void)context;
  (void)params;
  (void)input;
  (void)input_length;
  (void)output;
  (void)output_length;
  (void)output_params;
  return CPKT_SASL_FAIL;
}

static int empty_client(void *context, const cpkt_sasl_plugin_utils *utils,
                        int maximum_version, int *version_out,
                        const cpkt_sasl_client_plugin **plugins_out,
                        int *count_out) {
  (void)context;
  (void)utils;
  (void)maximum_version;
  *version_out = CPKT_SASL_CLIENT_PLUGIN_VERSION;
  *plugins_out = NULL;
  *count_out = 0;
  return CPKT_SASL_OK;
}

static int empty_server(void *context, const cpkt_sasl_plugin_utils *utils,
                        int maximum_version, int *version_out,
                        const cpkt_sasl_server_plugin **plugins_out,
                        int *count_out) {
  (void)context;
  (void)utils;
  (void)maximum_version;
  *version_out = CPKT_SASL_SERVER_PLUGIN_VERSION;
  *plugins_out = NULL;
  *count_out = 0;
  return CPKT_SASL_OK;
}

static int initialize_client(void *context, const cpkt_sasl_plugin_utils *utils,
                             int maximum_version, int *version_out,
                             const cpkt_sasl_client_plugin **plugins_out,
                             int *count_out) {
  static cpkt_sasl_client_plugin plugins[2];
  shared_context *state = (shared_context *)context;
  (void)maximum_version;
  memset(plugins, 0, sizeof(plugins));
  state->utils = utils;
  plugins[0].mechanism_name = "CPKT-IDLE-A";
  plugins[0].context = context;
  plugins[0].new_connection = client_new;
  plugins[0].step = client_step;
  plugins[0].idle = client_idle;
  plugins[0].free_context = client_free;
  plugins[1] = plugins[0];
  plugins[1].mechanism_name = "CPKT-IDLE-B";
  *version_out = CPKT_SASL_CLIENT_PLUGIN_VERSION;
  *plugins_out = plugins;
  *count_out = 2;
  return CPKT_SASL_OK;
}

static int initialize_server(void *context, const cpkt_sasl_plugin_utils *utils,
                             int maximum_version, int *version_out,
                             const cpkt_sasl_server_plugin **plugins_out,
                             int *count_out) {
  static cpkt_sasl_server_plugin plugins[2];
  shared_context *state = (shared_context *)context;
  (void)maximum_version;
  memset(plugins, 0, sizeof(plugins));
  state->utils = utils;
  plugins[0].context = context;
  plugins[0].new_connection = server_new;
  plugins[0].step = server_step;
  plugins[0].idle = server_idle;
  plugins[0].free_context = server_free;
  plugins[1] = plugins[0];
  if (state->condition == CPKT_SASL_CONTINUE) {
    plugins[0].mechanism_name = "CPKT-CONTINUE-A";
    plugins[1].mechanism_name = "CPKT-CONTINUE-B";
  } else if (state->condition == CPKT_SASL_NOUSER) {
    plugins[0].mechanism_name = "CPKT-NOUSER-A";
    plugins[1].mechanism_name = "CPKT-NOUSER-B";
  } else {
    plugins[0].mechanism_name = "CPKT-READY-A";
    plugins[1].mechanism_name = "CPKT-READY-B";
  }
  *version_out = CPKT_SASL_SERVER_PLUGIN_VERSION;
  *plugins_out = plugins;
  *count_out = 2;
  return state->condition;
}

static void condition_info(void *context, const cpkt_sasl_plugin_info *info,
                           int stage) {
  if (info == NULL)
    return;
  ++info_calls;
  if (stage != CPKT_SASL_INFO_LIST_MECHANISM ||
      info->condition != expected_condition || info->server_plugin == NULL ||
      info->server_plugin->context != context)
    callback_error = 1;
}

int main(void) {
  cpkt_sasl_callbacks callbacks;
  shared_context *client_context;
  shared_context *server_context;
  int status;
  int global_calls_before;
  CHECK(cpkt_sasl_idle(NULL) == 0);
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.log = quiet_log;
  CHECK(cpkt_sasl_client_initialize(&callbacks) == CPKT_SASL_OK);
  CHECK(cpkt_sasl_client_add_plugin("cpkt-empty", empty_client, NULL) ==
        CPKT_SASL_OK);
  client_context = (shared_context *)calloc(1, sizeof(*client_context));
  CHECK(client_context != NULL);
  client_context->references = 2;
  client_context->action = 1;
  CHECK(cpkt_sasl_client_add_plugin("cpkt-idle", initialize_client,
                                    client_context) == CPKT_SASL_OK);
  CHECK(cpkt_sasl_idle(NULL) == 1 && client_global_calls == 1);
  client_context->action = 0;
  CHECK(cpkt_sasl_idle(NULL) == 0 && client_global_calls == 3);
  expected_client = cpkt_sasl_client_new("idle-test", "localhost", NULL, NULL,
                                         &callbacks, 0, &status);
  CHECK(expected_client != NULL && status == CPKT_SASL_OK);
  client_context->action = 1;
  CHECK(expected_client->idle(expected_client) == 1 &&
        client_receiver_calls == 1);
  CHECK(cpkt_sasl_server_initialize(&callbacks, "cpkt-idle-test") ==
        CPKT_SASL_OK);
  CHECK(cpkt_sasl_server_add_plugin("cpkt-empty", empty_server, NULL) ==
        CPKT_SASL_OK);
  server_context = (shared_context *)calloc(1, sizeof(*server_context));
  CHECK(server_context != NULL);
  server_context->references = 6;
  server_context->action = 1;
  server_context->condition = CPKT_SASL_CONTINUE;
  CHECK(cpkt_sasl_server_add_plugin("cpkt-continue", initialize_server,
                                    server_context) == CPKT_SASL_OK);
  expected_condition = CPKT_SASL_CONTINUE;
  CHECK(cpkt_sasl_server_plugin_info("CPKT-CONTINUE-A", condition_info,
                                     server_context) == CPKT_SASL_OK);
  CHECK(info_calls == 1);
  server_context->condition = CPKT_SASL_NOUSER;
  CHECK(cpkt_sasl_server_add_plugin("cpkt-nouser", initialize_server,
                                    server_context) == CPKT_SASL_OK);
  expected_condition = CPKT_SASL_NOUSER;
  CHECK(cpkt_sasl_server_plugin_info("CPKT-NOUSER-A", condition_info,
                                     server_context) == CPKT_SASL_OK);
  CHECK(info_calls == 2);
  server_context->condition = CPKT_SASL_OK;
  CHECK(cpkt_sasl_server_add_plugin("cpkt-ready", initialize_server,
                                    server_context) == CPKT_SASL_OK);
  expected_server = cpkt_sasl_server_new("idle-test", "localhost", NULL, NULL,
                                         NULL, &callbacks, 0, &status);
  CHECK(expected_server != NULL && status == CPKT_SASL_OK);
  CHECK(expected_server->idle(expected_server) == 1 &&
        server_receiver_calls == 1);
  global_calls_before = client_global_calls;
  CHECK(cpkt_sasl_idle(NULL) == 1);
  CHECK(client_global_calls == global_calls_before &&
        server_receiver_calls == 1);
  CHECK(cpkt_sasl_client_initialize(&callbacks) == CPKT_SASL_OK);
  CHECK(cpkt_sasl_server_initialize(&callbacks, "cpkt-idle-test") ==
        CPKT_SASL_OK);
  cpkt_sasl_finish_all();
  CHECK(client_free_calls == 0 && server_free_calls == 0);
  CHECK(expected_client->idle(expected_client) == 1 &&
        client_receiver_calls == 2);
  CHECK(expected_server->idle(expected_server) == 1 &&
        server_receiver_calls == 2);
  expected_client->close(expected_client);
  expected_server->close(expected_server);
  CHECK(cpkt_sasl_client_finish() == CPKT_SASL_OK);
  CHECK(cpkt_sasl_server_finish() == CPKT_SASL_OK);
  CHECK(client_free_calls == 2 && server_free_calls == 6 &&
        final_free_calls == 2 && callback_error == 0);
  CHECK(cpkt_sasl_idle(NULL) == 0);
  return 0;
}
