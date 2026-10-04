#include <cpkt/sasl_plugin.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      fprintf(stderr, "registration failure at %d: %s\n", __LINE__,            \
              #expression);                                                    \
      return 1;                                                                \
    }                                                                          \
  } while (0)

typedef struct plugin_state {
  const cpkt_sasl_plugin_utils *utils;
  int releases;
  int idle_calls;
  int bad_utility;
} plugin_state;

static int fail_after = -1;
static plugin_state clients[3], servers[3], canon_state, aux_state;
static plugin_state nested_state;
static int client_info_count, server_info_count;
static int nested_status;

static void *fault_malloc(size_t size) {
  if (fail_after == 0) {
    fail_after = -1;
    return NULL;
  }
  if (fail_after > 0)
    --fail_after;
  return malloc(size);
}

static void *fault_calloc(size_t count, size_t size) {
  return calloc(count, size);
}

static void *fault_realloc(void *value, size_t size) {
  return realloc(value, size);
}

static void fault_free(void *value) { free(value); }

static void release_state(void *context, const cpkt_sasl_plugin_utils *utils) {
  plugin_state *state = (plugin_state *)context;
  void *scratch;
  ++state->releases;
  if (state->utils != utils || utils == NULL || utils->allocate == NULL ||
      utils->release == NULL) {
    state->bad_utility = 1;
    return;
  }
  scratch = utils->allocate(utils, 8);
  if (scratch == NULL)
    state->bad_utility = 1;
  else
    utils->release(utils, scratch);
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

static int client_idle(void *context, cpkt_sasl *receiver,
                       cpkt_sasl_client_params *params) {
  plugin_state *state = (plugin_state *)context;
  if (receiver != NULL && params != NULL) {
    ++state->idle_calls;
    return 1;
  }
  return 0;
}

static int server_idle(void *context, cpkt_sasl *receiver,
                       cpkt_sasl_server_params *params) {
  plugin_state *state = (plugin_state *)context;
  if (receiver != NULL && params != NULL) {
    ++state->idle_calls;
    return 1;
  }
  return 0;
}

static int initialize_client(void *context, const cpkt_sasl_plugin_utils *utils,
                             int maximum_version, int *version_out,
                             const cpkt_sasl_client_plugin **plugins_out,
                             int *count_out) {
  static cpkt_sasl_client_plugin plugins[3];
  static const char *names[3] = {"CPKT-FAULT-CLIENT-A", "CPKT-FAULT-CLIENT-B",
                                 "CPKT-FAULT-CLIENT-C"};
  int i;
  (void)context;
  if (maximum_version < CPKT_SASL_CLIENT_PLUGIN_VERSION)
    return CPKT_SASL_BADVERS;
  memset(plugins, 0, sizeof(plugins));
  for (i = 0; i < 3; ++i) {
    clients[i].utils = utils;
    plugins[i].mechanism_name = names[i];
    plugins[i].context = &clients[i];
    plugins[i].new_connection = client_new;
    plugins[i].step = client_step;
    plugins[i].free_context = release_state;
    plugins[i].idle = client_idle;
  }
  *version_out = CPKT_SASL_CLIENT_PLUGIN_VERSION;
  *plugins_out = plugins;
  *count_out = 3;
  return CPKT_SASL_OK;
}

static int empty_nested_client(void *context,
                               const cpkt_sasl_plugin_utils *utils,
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

static int nested_client(void *context, const cpkt_sasl_plugin_utils *utils,
                         int maximum_version, int *version_out,
                         const cpkt_sasl_client_plugin **plugins_out,
                         int *count_out) {
  static cpkt_sasl_client_plugin plugin;
  (void)context;
  if (maximum_version < CPKT_SASL_CLIENT_PLUGIN_VERSION)
    return CPKT_SASL_BADVERS;
  nested_status = cpkt_sasl_client_add_plugin("cpkt-nested-empty",
                                              empty_nested_client, NULL);
  if (nested_status != CPKT_SASL_OK)
    return nested_status;
  memset(&plugin, 0, sizeof(plugin));
  nested_state.utils = utils;
  plugin.mechanism_name = "CPKT-NESTED-OUTER";
  plugin.context = &nested_state;
  plugin.new_connection = client_new;
  plugin.step = client_step;
  plugin.free_context = release_state;
  *version_out = CPKT_SASL_CLIENT_PLUGIN_VERSION;
  *plugins_out = &plugin;
  *count_out = 1;
  return CPKT_SASL_OK;
}

static int initialize_server(void *context, const cpkt_sasl_plugin_utils *utils,
                             int maximum_version, int *version_out,
                             const cpkt_sasl_server_plugin **plugins_out,
                             int *count_out) {
  static cpkt_sasl_server_plugin plugins[3];
  static const char *names[3] = {"CPKT-FAULT-SERVER-A", "CPKT-FAULT-SERVER-B",
                                 "CPKT-FAULT-SERVER-C"};
  int i;
  (void)context;
  if (maximum_version < CPKT_SASL_SERVER_PLUGIN_VERSION)
    return CPKT_SASL_BADVERS;
  memset(plugins, 0, sizeof(plugins));
  for (i = 0; i < 3; ++i) {
    servers[i].utils = utils;
    plugins[i].mechanism_name = names[i];
    plugins[i].context = &servers[i];
    plugins[i].new_connection = server_new;
    plugins[i].step = server_step;
    plugins[i].free_context = release_state;
    plugins[i].idle = server_idle;
  }
  *version_out = CPKT_SASL_SERVER_PLUGIN_VERSION;
  *plugins_out = plugins;
  *count_out = 3;
  return CPKT_SASL_OK;
}

static int canon_user(void *context, cpkt_sasl_client_params *params,
                      const char *user, unsigned long length,
                      unsigned long flags, char *output, unsigned long capacity,
                      unsigned long *length_out) {
  (void)context;
  (void)params;
  (void)flags;
  if (length > capacity)
    return CPKT_SASL_BUFOVER;
  memcpy(output, user, (size_t)length);
  *length_out = length;
  return CPKT_SASL_OK;
}

static int initialize_canon(void *context, const cpkt_sasl_plugin_utils *utils,
                            int maximum_version, int *version_out,
                            const cpkt_sasl_canonicalizer_plugin **plugin_out,
                            const char *name) {
  static cpkt_sasl_canonicalizer_plugin plugin;
  (void)context;
  (void)name;
  if (maximum_version < CPKT_SASL_CANONICALIZER_PLUGIN_VERSION)
    return CPKT_SASL_BADVERS;
  memset(&plugin, 0, sizeof(plugin));
  canon_state.utils = utils;
  plugin.context = &canon_state;
  plugin.name = "CPKT-FAULT-CANON";
  plugin.free_context = release_state;
  plugin.client = canon_user;
  *version_out = CPKT_SASL_CANONICALIZER_PLUGIN_VERSION;
  *plugin_out = &plugin;
  return CPKT_SASL_OK;
}

static int aux_lookup(void *context, cpkt_sasl_server_params *params,
                      unsigned long flags, const char *user,
                      unsigned long length) {
  (void)context;
  (void)params;
  (void)flags;
  (void)user;
  (void)length;
  return CPKT_SASL_OK;
}

static int initialize_aux(void *context, const cpkt_sasl_plugin_utils *utils,
                          int maximum_version, int *version_out,
                          const cpkt_sasl_auxiliary_plugin **plugin_out,
                          const char *name) {
  static cpkt_sasl_auxiliary_plugin plugin;
  (void)context;
  (void)name;
  if (maximum_version < CPKT_SASL_AUXILIARY_PLUGIN_VERSION)
    return CPKT_SASL_BADVERS;
  memset(&plugin, 0, sizeof(plugin));
  aux_state.utils = utils;
  plugin.context = &aux_state;
  plugin.name = "CPKT-FAULT-AUX";
  plugin.free_context = release_state;
  plugin.lookup = aux_lookup;
  *version_out = CPKT_SASL_AUXILIARY_PLUGIN_VERSION;
  *plugin_out = &plugin;
  return CPKT_SASL_OK;
}

static void client_info(void *context, const cpkt_sasl_plugin_info *info,
                        int stage) {
  (void)context;
  if (stage == CPKT_SASL_INFO_LIST_MECHANISM && info != NULL &&
      info->client_plugin != NULL &&
      info->client_plugin->context == &clients[0])
    ++client_info_count;
}

static void server_info(void *context, const cpkt_sasl_plugin_info *info,
                        int stage) {
  (void)context;
  if (stage == CPKT_SASL_INFO_LIST_MECHANISM && info != NULL &&
      info->server_plugin != NULL &&
      info->server_plugin->context == &servers[0])
    ++server_info_count;
}

int main(void) {
  cpkt_sasl *client, *server;
  int status;
  cpkt_sasl_set_allocators(fault_malloc, fault_calloc, fault_realloc,
                           fault_free);
  CHECK(cpkt_sasl_client_initialize(NULL) == CPKT_SASL_OK);
  CHECK(cpkt_sasl_server_initialize(NULL, "cpkt-fault") == CPKT_SASL_OK);

  fail_after = 2;
  CHECK(cpkt_sasl_client_add_plugin("cpkt-fault-client", initialize_client,
                                    NULL) == CPKT_SASL_NOMEM);
  CHECK(clients[0].releases == 0 && clients[1].releases == 1 &&
        clients[2].releases == 1 && !clients[1].bad_utility &&
        !clients[2].bad_utility);
  CHECK(cpkt_sasl_client_plugin_info("CPKT-FAULT-CLIENT-A", client_info,
                                     NULL) == CPKT_SASL_OK &&
        client_info_count == 1);
  client = cpkt_sasl_client_new("fault-test", "localhost", NULL, NULL, NULL, 0,
                                &status);
  CHECK(client != NULL && status == CPKT_SASL_OK);
  CHECK(client->idle(client) == 1 && clients[0].idle_calls == 1);
  client->close(client);

  fail_after = 2;
  CHECK(cpkt_sasl_server_add_plugin("cpkt-fault-server", initialize_server,
                                    NULL) == CPKT_SASL_NOMEM);
  CHECK(servers[0].releases == 0 && servers[1].releases == 1 &&
        servers[2].releases == 1 && !servers[1].bad_utility &&
        !servers[2].bad_utility);
  CHECK(cpkt_sasl_server_plugin_info("CPKT-FAULT-SERVER-A", server_info,
                                     NULL) == CPKT_SASL_OK &&
        server_info_count == 1);
  server = cpkt_sasl_server_new("fault-test", "localhost", NULL, NULL, NULL,
                                NULL, 0, &status);
  CHECK(server != NULL && status == CPKT_SASL_OK);
  CHECK(server->idle(server) == 1 && servers[0].idle_calls == 1);
  server->close(server);

  fail_after = 0;
  CHECK(cpkt_sasl_canonicalizer_add_plugin("cpkt-fault-canon", initialize_canon,
                                           NULL) == CPKT_SASL_NOMEM);
  CHECK(canon_state.releases == 1 && !canon_state.bad_utility);
  fail_after = 0;
  CHECK(cpkt_sasl_auxiliary_add_plugin("cpkt-fault-aux", initialize_aux,
                                       NULL) == CPKT_SASL_NOMEM);
  CHECK(aux_state.releases == 1 && !aux_state.bad_utility);
  CHECK(cpkt_sasl_client_add_plugin("cpkt-nested-outer", nested_client, NULL) ==
            CPKT_SASL_OK &&
        nested_status == CPKT_SASL_OK);

  CHECK(cpkt_sasl_client_finish() == CPKT_SASL_OK);
  CHECK(cpkt_sasl_server_finish() == CPKT_SASL_OK);
  CHECK(clients[0].releases == 1 && servers[0].releases == 1 &&
        clients[1].releases == 1 && clients[2].releases == 1 &&
        servers[1].releases == 1 && servers[2].releases == 1 &&
        canon_state.releases == 1 && aux_state.releases == 1 &&
        nested_state.releases == 1 && !nested_state.bad_utility &&
        !clients[0].bad_utility && !servers[0].bad_utility);
  return 0;
}
