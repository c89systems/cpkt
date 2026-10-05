#include <cpkt/sasl_plugin.h>

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <sasl/sasl.h>
#include <sasl/saslplug.h>

#include "sasl_private.h"

typedef struct cpkt_sasl_client_registration cpkt_sasl_client_registration;

typedef struct cpkt_sasl_client_record {
  cpkt_sasl_client_plugin public_plugin;
  cpkt_sasl_client_registration *registration;
  int freed;
  int adopted;
} cpkt_sasl_client_record;

struct cpkt_sasl_client_registration {
  cpkt_sasl_client_record *records;
  sasl_client_plug_t *native_plugins;
  int count;
  const cpkt_sasl_plugin_utils *utils;
  struct cpkt_sasl_client_registration *next;
};

typedef struct cpkt_sasl_client_connection {
  cpkt_sasl_plugin_output_bridge output_bridge;
  cpkt_sasl_client_record *record;
  const cpkt_sasl_plugin_utils *utils;
  void *application_context;
  cpkt_sasl_interaction *application_interactions;
  sasl_interact_t *native_interactions;
} cpkt_sasl_client_connection;

typedef struct cpkt_sasl_client_pending {
  cpkt_sasl_client_plugin_init initialize;
  void *context;
  cpkt_sasl_client_registration *registration;
} cpkt_sasl_client_pending;

static pthread_key_t cpkt_sasl_client_pending_key;
static pthread_once_t cpkt_sasl_client_pending_once = PTHREAD_ONCE_INIT;
static int cpkt_sasl_client_pending_key_status;
static pthread_mutex_t cpkt_sasl_client_lock = PTHREAD_MUTEX_INITIALIZER;
static cpkt_sasl_client_registration *cpkt_sasl_client_registrations;

static void
cpkt_sasl_client_adoption_observer(client_sasl_mechanism_t *mechanism,
                                   sasl_info_callback_stage_t stage,
                                   void *context) {
  cpkt_sasl_client_registration *reg = (cpkt_sasl_client_registration *)context;
  int i;
  if (stage != SASL_INFO_LIST_MECH || mechanism == NULL)
    return;
  for (i = 0; i < reg->count; ++i)
    if (mechanism->plug == &reg->native_plugins[i])
      reg->records[i].adopted = 1;
}

static void
cpkt_sasl_client_release_public(const cpkt_sasl_client_plugin *plugins,
                                int count, cpkt_sasl_plugin_utils *utils) {
  int i;
  if (plugins == NULL || count < 1)
    return;
  for (i = 0; i < count; ++i) {
    if (plugins[i].free_context == NULL)
      continue;
    plugins[i].free_context(plugins[i].context, utils);
  }
}

static void cpkt_sasl_client_release_record(cpkt_sasl_client_record *record) {
  cpkt_sasl_client_registration *reg = record->registration;
  if (record->freed || record->public_plugin.free_context == NULL)
    return;
  record->freed = 1;
  record->public_plugin.free_context(record->public_plugin.context, reg->utils);
}

static void cpkt_sasl_client_make_key(void) {
  cpkt_sasl_client_pending_key_status =
      pthread_key_create(&cpkt_sasl_client_pending_key, free);
}

static int cpkt_sasl_client_new_native(void *context,
                                       sasl_client_params_t *native_params,
                                       void **connection_out) {
  cpkt_sasl_client_record *record = (cpkt_sasl_client_record *)context;
  cpkt_sasl_client_connection *connection;
  cpkt_sasl_plugin_client_view view;
  int status;
  if (record == NULL || native_params == NULL || connection_out == NULL)
    return SASL_BADPARAM;
  *connection_out = NULL;
  connection = (cpkt_sasl_client_connection *)calloc(1, sizeof(*connection));
  if (connection == NULL)
    return SASL_NOMEM;
  connection->record = record;
  cpkt_sasl_plugin_client_params_from_native(&view, native_params);
  if (view.params.utils == NULL) {
    free(connection);
    return SASL_NOMEM;
  }
  connection->utils = view.params.utils;
  status = record->public_plugin.new_connection(
      record->public_plugin.context, &view.params,
      &connection->application_context);
  if (status != SASL_OK) {
    if (connection->application_context != NULL &&
        record->public_plugin.dispose_connection != NULL)
      record->public_plugin.dispose_connection(connection->application_context,
                                               view.params.utils);
    free(connection);
    return status;
  }
  *connection_out = connection;
  return SASL_OK;
}

static int
cpkt_sasl_client_interactions_to_native(cpkt_sasl_client_connection *connection,
                                        cpkt_sasl_interaction *public_list,
                                        sasl_interact_t **native_out) {
  sasl_interact_t *native_list;
  size_t count = 0, i;
  if (native_out == NULL)
    return public_list == NULL ? SASL_OK : SASL_BADPARAM;
  *native_out = NULL;
  if (public_list == NULL)
    return SASL_OK;
  while (public_list[count].id != SASL_CB_LIST_END) {
    if (count == (size_t)-1 ||
        count >= ((size_t)-1) / sizeof(*native_list) - 1U)
      return SASL_BADPARAM;
    ++count;
  }
  native_list = (sasl_interact_t *)calloc(count + 1U, sizeof(*native_list));
  if (native_list == NULL)
    return SASL_NOMEM;
  for (i = 0; i <= count; ++i) {
    if (public_list[i].result_byte_count > UINT_MAX) {
      free(native_list);
      return SASL_BADPARAM;
    }
    native_list[i].id = public_list[i].id;
    native_list[i].challenge = public_list[i].challenge;
    native_list[i].prompt = public_list[i].prompt;
    native_list[i].defresult = public_list[i].default_result;
    native_list[i].result = public_list[i].result;
    native_list[i].len = (unsigned)public_list[i].result_byte_count;
  }
  free(connection->native_interactions);
  connection->native_interactions = native_list;
  connection->application_interactions = public_list;
  *native_out = native_list;
  return SASL_OK;
}

static int cpkt_sasl_client_step_native(
    void *context, sasl_client_params_t *native_params,
    const char *server_input, unsigned input_length,
    sasl_interact_t **interaction_out, const char **client_output,
    unsigned *output_length, sasl_out_params_t *native_output) {
  cpkt_sasl_client_connection *connection =
      (cpkt_sasl_client_connection *)context;
  cpkt_sasl_plugin_client_view view;
  cpkt_sasl_plugin_output public_output;
  cpkt_sasl_interaction *public_interactions = NULL;
  unsigned long public_length = 0;
  size_t i;
  int status;
  if (connection == NULL || native_params == NULL || native_output == NULL)
    return SASL_BADPARAM;
  if (interaction_out != NULL &&
      *interaction_out == connection->native_interactions &&
      connection->application_interactions != NULL) {
    for (i = 0; connection->native_interactions[i].id != SASL_CB_LIST_END;
         ++i) {
      connection->application_interactions[i].result =
          connection->native_interactions[i].result;
      connection->application_interactions[i].result_byte_count =
          connection->native_interactions[i].len;
    }
    public_interactions = connection->application_interactions;
  }
  cpkt_sasl_plugin_client_params_from_native(&view, native_params);
  if (view.params.utils == NULL)
    return SASL_NOMEM;
  cpkt_sasl_plugin_output_from_native(&public_output, native_output);
  status = connection->record->public_plugin.step(
      connection->application_context, &view.params, server_input, input_length,
      interaction_out == NULL ? NULL : &public_interactions, client_output,
      &public_length, &public_output);
  if (public_length > UINT_MAX)
    return SASL_BADPARAM;
  if (output_length != NULL)
    *output_length = (unsigned)public_length;
  if (status == SASL_INTERACT) {
    int convert = cpkt_sasl_client_interactions_to_native(
        connection, public_interactions, interaction_out);
    if (convert != SASL_OK)
      return convert;
  } else if (interaction_out != NULL) {
    *interaction_out = NULL;
  }
  if (status == SASL_OK || status == SASL_CONTINUE) {
    int convert = cpkt_sasl_plugin_output_to_native(
        &public_output, native_output, &connection->output_bridge);
    if (convert != SASL_OK)
      return convert;
  }
  return status;
}

static void cpkt_sasl_client_dispose_native(void *context,
                                            const sasl_utils_t *native_utils) {
  cpkt_sasl_client_connection *connection =
      (cpkt_sasl_client_connection *)context;
  (void)native_utils;
  if (connection == NULL)
    return;
  if (connection->record->public_plugin.dispose_connection != NULL)
    connection->record->public_plugin.dispose_connection(
        connection->application_context, connection->utils);
  free(connection->native_interactions);
  free(connection);
}

static void cpkt_sasl_client_free_native(void *context,
                                         const sasl_utils_t *native_utils) {
  cpkt_sasl_client_record *record = (cpkt_sasl_client_record *)context;
  (void)native_utils;
  if (record != NULL)
    cpkt_sasl_client_release_record(record);
}

static int cpkt_sasl_client_idle_native(void *context, void *conn_context,
                                        sasl_client_params_t *native_params) {
  cpkt_sasl_client_record *record = (cpkt_sasl_client_record *)context;
  cpkt_sasl_plugin_client_view view;
  cpkt_sasl *connection =
      cpkt_sasl_public_for_native((sasl_conn_t *)conn_context);
  if (record == NULL || record->public_plugin.idle == NULL)
    return 0;
  if (native_params == NULL)
    return record->public_plugin.idle(record->public_plugin.context, connection,
                                      NULL);
  cpkt_sasl_plugin_client_params_from_native(&view, native_params);
  if (view.params.utils == NULL)
    return 0;
  return record->public_plugin.idle(record->public_plugin.context, connection,
                                    &view.params);
}

static int cpkt_sasl_client_init_native(const sasl_utils_t *native_utils,
                                        int max_version, int *version_out,
                                        sasl_client_plug_t **plugins_out,
                                        int *count_out) {
  cpkt_sasl_client_pending *pending =
      (cpkt_sasl_client_pending *)pthread_getspecific(
          cpkt_sasl_client_pending_key);
  const cpkt_sasl_client_plugin *public_plugins = NULL;
  cpkt_sasl_client_registration *reg;
  cpkt_sasl_plugin_utils *utils;
  int count = 0, status, i;
  if (pending == NULL || pending->initialize == NULL || version_out == NULL ||
      plugins_out == NULL || count_out == NULL)
    return SASL_BADPARAM;
  *plugins_out = NULL;
  *count_out = 0;
  utils = cpkt_sasl_plugin_utils_for_native(native_utils);
  if (utils == NULL)
    return SASL_NOMEM;
  cpkt_sasl_plugin_utils_set_global_option_context(
      native_utils, 0, cpkt_sasl_global_option_application_context(0));
  status = pending->initialize(pending->context, utils, max_version,
                               version_out, &public_plugins, &count);
  if (status != SASL_OK)
    return status;
  if (*version_out != SASL_CLIENT_PLUG_VERSION || count < 0 ||
      (count != 0 && public_plugins == NULL)) {
    cpkt_sasl_client_release_public(public_plugins, count, utils);
    return SASL_BADVERS;
  }
  if (count == 0)
    return SASL_OK;
  reg = (cpkt_sasl_client_registration *)calloc(1, sizeof(*reg));
  if (reg == NULL) {
    cpkt_sasl_client_release_public(public_plugins, count, utils);
    return SASL_NOMEM;
  }
  reg->records =
      (cpkt_sasl_client_record *)calloc((size_t)count, sizeof(*reg->records));
  reg->native_plugins =
      (sasl_client_plug_t *)calloc((size_t)count, sizeof(*reg->native_plugins));
  if (reg->records == NULL || reg->native_plugins == NULL) {
    cpkt_sasl_client_release_public(public_plugins, count, utils);
    free(reg->native_plugins);
    free(reg->records);
    free(reg);
    return SASL_NOMEM;
  }
  reg->count = count;
  reg->utils = utils;
  for (i = 0; i < count; ++i) {
    cpkt_sasl_client_record *record = &reg->records[i];
    sasl_client_plug_t *native = &reg->native_plugins[i];
    record->public_plugin = public_plugins[i];
    record->registration = reg;
    if (record->public_plugin.mechanism_name == NULL ||
        record->public_plugin.new_connection == NULL ||
        record->public_plugin.step == NULL ||
        record->public_plugin.maximum_ssf > UINT_MAX ||
        record->public_plugin.security_flags > UINT_MAX ||
        record->public_plugin.features > UINT_MAX) {
      cpkt_sasl_client_release_public(public_plugins, count, utils);
      free(reg->records);
      free(reg->native_plugins);
      free(reg);
      return SASL_BADPARAM;
    }
    native->mech_name = record->public_plugin.mechanism_name;
    native->max_ssf = (sasl_ssf_t)record->public_plugin.maximum_ssf;
    native->security_flags = (unsigned)record->public_plugin.security_flags;
    native->features = (unsigned)record->public_plugin.features;
    native->required_prompts = record->public_plugin.required_prompts;
    native->glob_context = record;
    native->mech_new = cpkt_sasl_client_new_native;
    native->mech_step = cpkt_sasl_client_step_native;
    native->mech_dispose = cpkt_sasl_client_dispose_native;
    native->mech_free = cpkt_sasl_client_free_native;
    native->idle = record->public_plugin.idle == NULL
                       ? NULL
                       : cpkt_sasl_client_idle_native;
  }
  pending->registration = reg;
  *plugins_out = reg->native_plugins;
  *count_out = count;
  return SASL_OK;
}

/** C89 facade contract for cpkt_sasl_client_add_plugin; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_client_add_plugin(const char *name,
                                cpkt_sasl_client_plugin_init initialize,
                                void *context) {
  cpkt_sasl_client_pending *pending, *previous;
  int status, provider_status, i;
  if (name == NULL || initialize == NULL)
    return SASL_BADPARAM;
  if (pthread_once(&cpkt_sasl_client_pending_once, cpkt_sasl_client_make_key) !=
          0 ||
      cpkt_sasl_client_pending_key_status != 0)
    return SASL_FAIL;
  pending = (cpkt_sasl_client_pending *)calloc(1, sizeof(*pending));
  if (pending == NULL)
    return SASL_NOMEM;
  previous = (cpkt_sasl_client_pending *)pthread_getspecific(
      cpkt_sasl_client_pending_key);
  pending->initialize = initialize;
  pending->context = context;
  if (pthread_setspecific(cpkt_sasl_client_pending_key, pending) != 0) {
    free(pending);
    return SASL_FAIL;
  }
  provider_status = sasl_client_add_plugin(name, cpkt_sasl_client_init_native);
  if (pending->registration != NULL && provider_status != SASL_OK) {
    cpkt_sasl_client_registration *reg = pending->registration;
    (void)sasl_client_plugin_info(NULL, cpkt_sasl_client_adoption_observer,
                                  reg);
    for (i = 0; i < reg->count; ++i)
      if (!reg->records[i].adopted)
        cpkt_sasl_client_release_record(&reg->records[i]);
  }
  status = provider_status;
  if (pthread_setspecific(cpkt_sasl_client_pending_key, previous) != 0)
    status = SASL_FAIL;
  if (pending->registration != NULL) {
    (void)pthread_mutex_lock(&cpkt_sasl_client_lock);
    pending->registration->next = cpkt_sasl_client_registrations;
    cpkt_sasl_client_registrations = pending->registration;
    (void)pthread_mutex_unlock(&cpkt_sasl_client_lock);
  }
  if (pthread_getspecific(cpkt_sasl_client_pending_key) != pending)
    free(pending);
  return status;
}

__attribute__((visibility("hidden"))) void
cpkt_sasl_client_plugins_cleanup(void) {
  cpkt_sasl_client_registration *reg, *next;
  (void)pthread_mutex_lock(&cpkt_sasl_client_lock);
  reg = cpkt_sasl_client_registrations;
  cpkt_sasl_client_registrations = NULL;
  (void)pthread_mutex_unlock(&cpkt_sasl_client_lock);
  while (reg != NULL) {
    next = reg->next;
    free(reg->records);
    free(reg->native_plugins);
    free(reg);
    reg = next;
  }
}

const cpkt_sasl_client_plugin *
cpkt_sasl_client_plugin_for_native(const sasl_client_plug_t *native) {
  cpkt_sasl_client_registration *reg;
  const cpkt_sasl_client_plugin *result = NULL;
  int i;
  (void)pthread_mutex_lock(&cpkt_sasl_client_lock);
  for (reg = cpkt_sasl_client_registrations; reg != NULL; reg = reg->next) {
    for (i = 0; i < reg->count; ++i) {
      if (native == &reg->native_plugins[i] ||
          (native != NULL && native->glob_context == &reg->records[i])) {
        result = &reg->records[i].public_plugin;
        break;
      }
    }
    if (result != NULL)
      break;
  }
  (void)pthread_mutex_unlock(&cpkt_sasl_client_lock);
  return result;
}
