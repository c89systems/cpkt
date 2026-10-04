#include <cpkt/sasl.h>
#include <cpkt/sasl_plugin.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/sasl_private.h"

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      fprintf(stderr, "property coherence line %d: %s\n", __LINE__,            \
              #expression);                                                    \
      return 1;                                                                \
    }                                                                          \
  } while (0)

typedef struct coherence_state {
  cpkt_sasl_property_context *auxiliary;
  const cpkt_sasl_property_value *first;
  int calls;
  int errors;
  int append_ok;
} coherence_state;

static const char *role_name[] = {"role", NULL};
static const char *both_names[] = {"role", "extra", NULL};

#if defined(__linux__)
static int fail_view_growth;
void *__real_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size) {
  if (fail_view_growth && count == 2 &&
      size == sizeof(cpkt_sasl_property_value)) {
    fail_view_growth = 0;
    return NULL;
  }
  return __real_calloc(count, size);
}
#endif

static int check_values(cpkt_sasl_property_context *context,
                        const char *const *names, size_t expected_count,
                        const char *role_first, const char *role_second,
                        const char *extra) {
  const cpkt_sasl_property_value *views;
  cpkt_sasl_property_value selected[2];
  size_t count;
  size_t selected_count;
  size_t i;
  unsigned long role_values;
  int found;
  count = 99;
  views = cpkt_sasl_property_get(context, &count);
  if (count != expected_count || (expected_count != 0 && views == NULL) ||
      (expected_count == 0 && views != NULL))
    return 0;
  if (expected_count == 0)
    return 1;
  memset(selected, 0, sizeof(selected));
  found = cpkt_sasl_property_getnames(context, names, selected, expected_count,
                                      &selected_count);
  if (found < 0 || selected_count != expected_count)
    return 0;
  for (i = 0; i < expected_count; ++i) {
    if (views[i].name == NULL || selected[i].name == NULL ||
        strcmp(views[i].name, names[i]) != 0 ||
        strcmp(selected[i].name, names[i]) != 0 ||
        views[i].value_count != selected[i].value_count ||
        views[i].total_value_bytes != selected[i].total_value_bytes)
      return 0;
  }
  role_values = role_first == NULL ? 0UL : role_second == NULL ? 1UL : 2UL;
  if (views[0].value_count != role_values)
    return 0;
  if (role_first != NULL &&
      (views[0].values == NULL || views[0].values[0] == NULL ||
       strcmp(views[0].values[0], role_first) != 0))
    return 0;
  if (role_second != NULL &&
      (views[0].values == NULL || views[0].values[1] == NULL ||
       strcmp(views[0].values[1], role_second) != 0))
    return 0;
  if (expected_count == 2) {
    if (views[1].value_count != (extra == NULL ? 0UL : 1UL))
      return 0;
    if (extra != NULL &&
        (views[1].values == NULL || views[1].values[0] == NULL ||
         strcmp(views[1].values[0], extra) != 0))
      return 0;
  }
  return 1;
}

static int plugin_new(void *context, cpkt_sasl_server_params *params,
                      const char *challenge, unsigned long challenge_length,
                      void **connection_out) {
  coherence_state *state;
  const cpkt_sasl_property_value *after;
  size_t count;
  (void)challenge;
  (void)challenge_length;
  state = (coherence_state *)context;
  ++state->calls;
  *connection_out = NULL;
  if (params == NULL || params->properties == NULL)
    ++state->errors;
  else {
    if (cpkt_sasl_property_set(params->properties, "role", "new", 3) !=
        CPKT_SASL_OK)
      ++state->errors;
    state->append_ok =
        check_values(state->auxiliary, role_name, 1, "old", "new", NULL);
    if (!state->append_ok)
      ++state->errors;
    after = cpkt_sasl_property_get(state->auxiliary, &count);
    if (after != state->first || count != 1)
      ++state->errors;
    cpkt_sasl_property_erase(params->properties, "role");
    if (!check_values(state->auxiliary, role_name, 1, NULL, NULL, NULL))
      ++state->errors;
    if (cpkt_sasl_property_set(params->properties, "role", "fresh", 5) !=
            CPKT_SASL_OK ||
        !check_values(state->auxiliary, role_name, 1, "fresh", NULL, NULL))
      ++state->errors;
    cpkt_sasl_property_clear(params->properties, 1);
    if (!check_values(state->auxiliary, role_name, 0, NULL, NULL, NULL))
      ++state->errors;
    if (cpkt_sasl_property_request(params->properties, both_names) !=
            CPKT_SASL_OK ||
        !check_values(state->auxiliary, both_names, 2, NULL, NULL, NULL))
      ++state->errors;
    if (cpkt_sasl_property_set(params->properties, "role", "again", 5) !=
            CPKT_SASL_OK ||
        cpkt_sasl_property_set(params->properties, "extra", "x", 1) !=
            CPKT_SASL_OK ||
        !check_values(state->auxiliary, both_names, 2, "again", NULL, "x"))
      ++state->errors;
    cpkt_sasl_property_clear(params->properties, 0);
    if (!check_values(state->auxiliary, both_names, 2, NULL, NULL, NULL))
      ++state->errors;
  }
  return CPKT_SASL_FAIL;
}

static int plugin_step(void *context, cpkt_sasl_server_params *params,
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

static int plugin_initialize(void *context, const cpkt_sasl_plugin_utils *utils,
                             int maximum_version, int *version_out,
                             const cpkt_sasl_server_plugin **plugins_out,
                             int *count_out) {
  static cpkt_sasl_server_plugin plugin;
  (void)utils;
  if (maximum_version < CPKT_SASL_SERVER_PLUGIN_VERSION)
    return CPKT_SASL_BADVERS;
  memset(&plugin, 0, sizeof(plugin));
  plugin.mechanism_name = "CPKTCOHERENCE";
  plugin.context = context;
  plugin.new_connection = plugin_new;
  plugin.step = plugin_step;
  *version_out = CPKT_SASL_SERVER_PLUGIN_VERSION;
  *plugins_out = &plugin;
  *count_out = 1;
  return CPKT_SASL_OK;
}

static int public_plugin_case(void) {
  coherence_state state;
  cpkt_sasl *server;
  const cpkt_sasl_property_value *again;
  const char *output;
  unsigned long output_length;
  size_t count;
  int status;
  memset(&state, 0, sizeof(state));
  CHECK(cpkt_sasl_server_initialize(NULL, "cpkt-coherence") == CPKT_SASL_OK);
  CHECK(cpkt_sasl_server_add_plugin("cpkt-coherence", plugin_initialize,
                                    &state) == CPKT_SASL_OK);
  server = cpkt_sasl_server_new("cpkt-coherence", "localhost", NULL, NULL, NULL,
                                NULL, 0, &status);
  CHECK(server != NULL && status == CPKT_SASL_OK);
  state.auxiliary = server->auxiliary_context(server);
  CHECK(state.auxiliary != NULL);
  CHECK(cpkt_sasl_property_request(state.auxiliary, role_name) == CPKT_SASL_OK);
  CHECK(cpkt_sasl_property_set(state.auxiliary, "role", "old", 3) ==
        CPKT_SASL_OK);
  CHECK(check_values(state.auxiliary, role_name, 1, "old", NULL, NULL));
  state.first = cpkt_sasl_property_get(state.auxiliary, &count);
  again = cpkt_sasl_property_get(state.auxiliary, &count);
  CHECK(again == state.first && count == 1);
  output = NULL;
  output_length = 0;
  status = server->server_start(server, "CPKTCOHERENCE", NULL, 0, &output,
                                &output_length);
  if (status != CPKT_SASL_FAIL || state.calls != 1 || state.errors != 0)
    fprintf(stderr, "plugin status=%d calls=%d errors=%d append_ok=%d\n",
            status, state.calls, state.errors, state.append_ok);
  CHECK(status == CPKT_SASL_FAIL && state.calls == 1 && state.errors == 0);
  cpkt_sasl_close(server);
  CHECK(cpkt_sasl_server_finish() == CPKT_SASL_OK);
  return 0;
}

static int native_peer_case(void) {
  cpkt_sasl_property_context *context;
  const cpkt_sasl_property_value *first;
  const cpkt_sasl_property_value *again;
  size_t count;
  context = cpkt_sasl_property_new(4);
  CHECK(context != NULL);
  CHECK(cpkt_sasl_property_request(context, role_name) == CPKT_SASL_OK);
  CHECK(cpkt_sasl_property_set(context, "role", "old", 3) == CPKT_SASL_OK);
  first = cpkt_sasl_property_get(context, &count);
  CHECK(first != NULL && count == 1);
  CHECK(prop_set(context->native, "role", "peer", 4) == SASL_OK);
  CHECK(check_values(context, role_name, 1, "old", "peer", NULL));
  again = cpkt_sasl_property_get(context, &count);
  CHECK(again == first && count == 1);
  prop_erase(context->native, "role");
  CHECK(check_values(context, role_name, 1, NULL, NULL, NULL));
  CHECK(prop_set(context->native, "role", "replaced", 8) == SASL_OK);
  CHECK(check_values(context, role_name, 1, "replaced", NULL, NULL));
  prop_clear(context->native, 1);
  CHECK(check_values(context, role_name, 0, NULL, NULL, NULL));
  CHECK(prop_request(context->native, (const char **)both_names) == SASL_OK);
#if defined(__linux__)
  fail_view_growth = 1;
  count = 99;
  CHECK(cpkt_sasl_property_get(context, &count) == NULL && count == 0 &&
        fail_view_growth == 0);
#endif
  CHECK(check_values(context, both_names, 2, NULL, NULL, NULL));
  CHECK(prop_set(context->native, "role", "r", 1) == SASL_OK);
  CHECK(prop_set(context->native, "extra", "e", 1) == SASL_OK);
  CHECK(check_values(context, both_names, 2, "r", NULL, "e"));
  cpkt_sasl_property_dispose(&context);
  CHECK(context == NULL);
  return 0;
}

int main(void) {
  if (public_plugin_case() != 0)
    return 1;
  if (native_peer_case() != 0)
    return 2;
  return 0;
}
