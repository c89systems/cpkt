#include <cpkt/sasl_plugin.h>

#include <sasl/sasl.h>
#include <sasl/saslplug.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      fprintf(stderr, "utility allocation failure at %d: %s\n", __LINE__,      \
              #expression);                                                    \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static int fail_next_calloc;
static int available_calls;
static int free_calls;
static int bad_utils;
static const cpkt_sasl_plugin_utils *retained_init_utils;
static const cpkt_sasl_plugin_utils *retained_connection_utils;
static const cpkt_sasl_plugin_utils *first_connection_utils;
static const sasl_server_plug_t *native_plugin;

void *__real_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size) {
  if (fail_next_calloc) {
    fail_next_calloc = 0;
    return NULL;
  }
  return __real_calloc(count, size);
}

static int available(void *context, cpkt_sasl_server_params *params,
                     void **connection_out) {
  void *scratch;
  (void)context;
  ++available_calls;
  if (params == NULL || params->utils == NULL || connection_out == NULL) {
    bad_utils = 1;
    return CPKT_SASL_BADPARAM;
  }
  retained_connection_utils = params->utils;
  scratch = params->utils->allocate(params->utils, 9);
  if (scratch == NULL) {
    bad_utils = 1;
    return CPKT_SASL_NOMEM;
  }
  params->utils->release(params->utils, scratch);
  *connection_out = NULL;
  return CPKT_SASL_OK;
}

static int new_connection(void *context, cpkt_sasl_server_params *params,
                          const char *challenge, unsigned long challenge_size,
                          void **connection_out) {
  (void)context;
  (void)params;
  (void)challenge;
  (void)challenge_size;
  *connection_out = NULL;
  return CPKT_SASL_FAIL;
}

static int step(void *context, cpkt_sasl_server_params *params,
                const char *input, unsigned long input_size,
                const char **output, unsigned long *output_size,
                cpkt_sasl_plugin_output *out_params) {
  (void)context;
  (void)params;
  (void)input;
  (void)input_size;
  (void)output;
  (void)output_size;
  (void)out_params;
  return CPKT_SASL_FAIL;
}

static void release_plugin(void *context, const cpkt_sasl_plugin_utils *utils) {
  void *scratch;
  (void)context;
  ++free_calls;
  if (utils != retained_init_utils || utils == NULL) {
    bad_utils = 1;
    return;
  }
  scratch = utils->allocate(utils, 7);
  if (scratch == NULL)
    bad_utils = 1;
  else
    utils->release(utils, scratch);
}

static int initialize(void *context, const cpkt_sasl_plugin_utils *utils,
                      int maximum_version, int *version_out,
                      const cpkt_sasl_server_plugin **plugins_out,
                      int *count_out) {
  static cpkt_sasl_server_plugin plugin;
  (void)context;
  if (utils == NULL || maximum_version < CPKT_SASL_SERVER_PLUGIN_VERSION)
    return CPKT_SASL_BADVERS;
  retained_init_utils = utils;
  memset(&plugin, 0, sizeof(plugin));
  plugin.mechanism_name = "CPKT-UTIL-FAULT";
  plugin.available = available;
  plugin.new_connection = new_connection;
  plugin.step = step;
  plugin.free_context = release_plugin;
  *version_out = CPKT_SASL_SERVER_PLUGIN_VERSION;
  *plugins_out = &plugin;
  *count_out = 1;
  return CPKT_SASL_OK;
}

static void observe_native(server_sasl_mechanism_t *mechanism,
                           sasl_info_callback_stage_t stage, void *context) {
  (void)context;
  if (stage == SASL_INFO_LIST_MECH && mechanism != NULL)
    native_plugin = mechanism->plug;
}

int main(void) {
  sasl_utils_t native_utils;
  sasl_server_params_t native_params;
  cpkt_sasl *receiver;
  void *connection = NULL;
  const char *output = NULL;
  unsigned long output_size = 0;
  int status;
  CHECK(cpkt_sasl_server_initialize(NULL, "utility-fault") == CPKT_SASL_OK);
  CHECK(cpkt_sasl_server_add_plugin("cpkt-utility-fault", initialize, NULL) ==
        CPKT_SASL_OK);
  CHECK(sasl_server_plugin_info("CPKT-UTIL-FAULT", observe_native, NULL) ==
        SASL_OK);
  CHECK(native_plugin != NULL && native_plugin->mech_avail != NULL);
  memset(&native_utils, 0, sizeof(native_utils));
  memset(&native_params, 0, sizeof(native_params));
  native_utils.malloc = malloc;
  native_utils.free = free;
  native_params.utils = &native_utils;
  fail_next_calloc = 1;
  CHECK(native_plugin->mech_avail(native_plugin->glob_context, &native_params,
                                  &connection) == SASL_NOMEM);
  CHECK(fail_next_calloc == 0 && available_calls == 0 && connection == NULL);
  CHECK(native_plugin->mech_avail(native_plugin->glob_context, &native_params,
                                  &connection) == SASL_OK);
  CHECK(available_calls == 1 && !bad_utils && connection == NULL &&
        retained_connection_utils != NULL);
  first_connection_utils = retained_connection_utils;
  CHECK(native_plugin->mech_avail(native_plugin->glob_context, &native_params,
                                  &connection) == SASL_OK);
  CHECK(available_calls == 2 && !bad_utils && connection == NULL &&
        retained_connection_utils == first_connection_utils);
  receiver = cpkt_sasl_server_new("utility-fault", "localhost", NULL, NULL,
                                  NULL, NULL, 0, &status);
  CHECK(receiver != NULL && status == CPKT_SASL_OK);
  (void)receiver->server_start(receiver, "CPKT-UTIL-FAULT", NULL, 0, &output,
                               &output_size);
  CHECK(available_calls >= 3 && !bad_utils &&
        retained_connection_utils != NULL);
  CHECK(receiver->idle(receiver) >= 0);
  receiver->close(receiver);
  CHECK(cpkt_sasl_server_finish() == CPKT_SASL_OK);
  CHECK(free_calls == 1 && !bad_utils);
  return 0;
}
