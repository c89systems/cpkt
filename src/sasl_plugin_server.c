#include <cpkt/sasl_plugin.h>

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>

#include <sasl/sasl.h>
#include <sasl/saslplug.h>

#include "sasl_private.h"

typedef struct cpkt_sasl_server_registration cpkt_sasl_server_registration;

typedef struct cpkt_sasl_server_record {
  cpkt_sasl_server_plugin public_plugin;
  cpkt_sasl_server_registration *registration;
  int freed;
  int adopted;
} cpkt_sasl_server_record;

struct cpkt_sasl_server_registration {
  cpkt_sasl_server_record *records;
  sasl_server_plug_t *native_plugins;
  int count;
  const cpkt_sasl_plugin_utils *utils;
  struct cpkt_sasl_server_registration *next;
};

typedef struct cpkt_sasl_server_connection {
  cpkt_sasl_plugin_output_bridge output_bridge;
  cpkt_sasl_server_record *record;
  const cpkt_sasl_plugin_utils *utils;
  void *application_context;
} cpkt_sasl_server_connection;

typedef struct cpkt_sasl_server_pending {
  cpkt_sasl_server_plugin_init initialize;
  void *context;
  cpkt_sasl_server_registration *registration;
} cpkt_sasl_server_pending;

static pthread_key_t cpkt_sasl_server_pending_key;
static pthread_once_t cpkt_sasl_server_pending_once = PTHREAD_ONCE_INIT;
static int cpkt_sasl_server_pending_key_status;
static pthread_mutex_t cpkt_sasl_server_lock = PTHREAD_MUTEX_INITIALIZER;
static cpkt_sasl_server_registration *cpkt_sasl_server_registrations;

static void
cpkt_sasl_server_adoption_observer(server_sasl_mechanism_t *mechanism,
                                   sasl_info_callback_stage_t stage,
                                   void *context) {
  cpkt_sasl_server_registration *reg = (cpkt_sasl_server_registration *)context;
  int i;
  if (stage != SASL_INFO_LIST_MECH || mechanism == NULL)
    return;
  for (i = 0; i < reg->count; ++i)
    if (mechanism->plug == &reg->native_plugins[i])
      reg->records[i].adopted = 1;
}

static void
cpkt_sasl_server_release_public(const cpkt_sasl_server_plugin *plugins,
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

static void cpkt_sasl_server_release_record(cpkt_sasl_server_record *record) {
  cpkt_sasl_server_registration *reg = record->registration;
  if (record->freed || record->public_plugin.free_context == NULL)
    return;
  record->freed = 1;
  record->public_plugin.free_context(record->public_plugin.context, reg->utils);
}

static void cpkt_sasl_server_make_key(void) {
  cpkt_sasl_server_pending_key_status =
      pthread_key_create(&cpkt_sasl_server_pending_key, free);
}

static int cpkt_sasl_server_new_native(void *context,
                                       sasl_server_params_t *native_params,
                                       const char *challenge,
                                       unsigned challenge_length,
                                       void **connection_out) {
  cpkt_sasl_server_record *record = (cpkt_sasl_server_record *)context;
  cpkt_sasl_server_connection *connection;
  cpkt_sasl_plugin_server_view view;
  int status;
  if (record == NULL || native_params == NULL || connection_out == NULL)
    return SASL_BADPARAM;
  *connection_out = NULL;
  connection = (cpkt_sasl_server_connection *)calloc(1, sizeof(*connection));
  if (connection == NULL)
    return SASL_NOMEM;
  connection->record = record;
  cpkt_sasl_plugin_server_params_from_native(&view, native_params);
  if (view.params.utils == NULL) {
    free(connection);
    return SASL_NOMEM;
  }
  connection->utils = view.params.utils;
  status = record->public_plugin.new_connection(
      record->public_plugin.context, &view.params, challenge, challenge_length,
      &connection->application_context);
  cpkt_sasl_property_discard_borrowed_views(&view.properties);
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

static int cpkt_sasl_server_step_native(
    void *context, sasl_server_params_t *native_params,
    const char *client_input, unsigned input_length, const char **server_output,
    unsigned *output_length, sasl_out_params_t *native_output) {
  cpkt_sasl_server_connection *connection =
      (cpkt_sasl_server_connection *)context;
  cpkt_sasl_plugin_server_view view;
  cpkt_sasl_plugin_output public_output;
  unsigned long public_length = 0;
  int status;
  if (connection == NULL || native_params == NULL || native_output == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_plugin_server_params_from_native(&view, native_params);
  if (view.params.utils == NULL)
    return SASL_NOMEM;
  cpkt_sasl_plugin_output_from_native(&public_output, native_output);
  status = connection->record->public_plugin.step(
      connection->application_context, &view.params, client_input, input_length,
      server_output, &public_length, &public_output);
  cpkt_sasl_property_discard_borrowed_views(&view.properties);
  if (public_length > UINT_MAX)
    return SASL_BADPARAM;
  if (output_length != NULL)
    *output_length = (unsigned)public_length;
  if (status == SASL_OK || status == SASL_CONTINUE) {
    int convert = cpkt_sasl_plugin_output_to_native(
        &public_output, native_output, &connection->output_bridge);
    if (convert != SASL_OK)
      return convert;
  }
  return status;
}

static void cpkt_sasl_server_dispose_native(void *context,
                                            const sasl_utils_t *native_utils) {
  cpkt_sasl_server_connection *connection =
      (cpkt_sasl_server_connection *)context;
  (void)native_utils;
  if (connection == NULL)
    return;
  if (connection->record->public_plugin.dispose_connection != NULL)
    connection->record->public_plugin.dispose_connection(
        connection->application_context, connection->utils);
  free(connection);
}

static void cpkt_sasl_server_free_native(void *context,
                                         const sasl_utils_t *native_utils) {
  cpkt_sasl_server_record *record = (cpkt_sasl_server_record *)context;
  (void)native_utils;
  if (record != NULL)
    cpkt_sasl_server_release_record(record);
}

static int cpkt_sasl_server_setpass_native(
    void *context, sasl_server_params_t *native_params, const char *user,
    const char *password, unsigned password_length, const char *old_password,
    unsigned old_password_length, unsigned flags) {
  cpkt_sasl_server_record *record = (cpkt_sasl_server_record *)context;
  cpkt_sasl_plugin_server_view view;
  int status;
  if (record == NULL || native_params == NULL ||
      record->public_plugin.set_password == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_plugin_server_params_from_native(&view, native_params);
  if (view.params.utils == NULL)
    return SASL_NOMEM;
  status = record->public_plugin.set_password(
      record->public_plugin.context, &view.params, user, password,
      password_length, old_password, old_password_length, flags);
  cpkt_sasl_property_discard_borrowed_views(&view.properties);
  return status;
}

static int cpkt_sasl_server_user_query_native(
    void *context, sasl_server_params_t *native_params, const char *user,
    int maximum_mechanisms, const char **mechanisms_out) {
  cpkt_sasl_server_record *record = (cpkt_sasl_server_record *)context;
  cpkt_sasl_plugin_server_view view;
  int status;
  if (record == NULL || native_params == NULL ||
      record->public_plugin.user_query == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_plugin_server_params_from_native(&view, native_params);
  if (view.params.utils == NULL)
    return SASL_NOMEM;
  status = record->public_plugin.user_query(record->public_plugin.context,
                                            &view.params, user,
                                            maximum_mechanisms, mechanisms_out);
  cpkt_sasl_property_discard_borrowed_views(&view.properties);
  return status;
}

static int cpkt_sasl_server_idle_native(void *context, void *conn_context,
                                        sasl_server_params_t *native_params) {
  cpkt_sasl_server_record *record = (cpkt_sasl_server_record *)context;
  cpkt_sasl_plugin_server_view view;
  cpkt_sasl *connection =
      cpkt_sasl_public_for_native((sasl_conn_t *)conn_context);
  int status;
  if (record == NULL || record->public_plugin.idle == NULL)
    return 0;
  if (native_params == NULL)
    return record->public_plugin.idle(record->public_plugin.context, connection,
                                      NULL);
  cpkt_sasl_plugin_server_params_from_native(&view, native_params);
  if (view.params.utils == NULL)
    return 0;
  status = record->public_plugin.idle(record->public_plugin.context, connection,
                                      &view.params);
  cpkt_sasl_property_discard_borrowed_views(&view.properties);
  return status;
}

static int cpkt_sasl_server_avail_native(void *context,
                                         sasl_server_params_t *native_params,
                                         void **connection_out) {
  cpkt_sasl_server_record *record = (cpkt_sasl_server_record *)context;
  cpkt_sasl_server_connection *connection;
  cpkt_sasl_plugin_server_view view;
  void *application_context = NULL;
  int status;
  if (record == NULL || native_params == NULL || connection_out == NULL ||
      record->public_plugin.available == NULL)
    return SASL_BADPARAM;
  *connection_out = NULL;
  cpkt_sasl_plugin_server_params_from_native(&view, native_params);
  if (view.params.utils == NULL)
    return SASL_NOMEM;
  status = record->public_plugin.available(record->public_plugin.context,
                                           &view.params, &application_context);
  cpkt_sasl_property_discard_borrowed_views(&view.properties);
  if (status != SASL_OK && application_context != NULL &&
      record->public_plugin.dispose_connection != NULL)
    record->public_plugin.dispose_connection(application_context,
                                             view.params.utils);
  if (status != SASL_OK || application_context == NULL)
    return status;
  connection = (cpkt_sasl_server_connection *)calloc(1, sizeof(*connection));
  if (connection == NULL) {
    if (record->public_plugin.dispose_connection != NULL)
      record->public_plugin.dispose_connection(application_context,
                                               view.params.utils);
    return SASL_NOMEM;
  }
  connection->record = record;
  connection->utils = view.params.utils;
  connection->application_context = application_context;
  *connection_out = connection;
  return status;
}

static int cpkt_sasl_server_init_native(const sasl_utils_t *native_utils,
                                        int max_version, int *version_out,
                                        sasl_server_plug_t **plugins_out,
                                        int *count_out) {
  cpkt_sasl_server_pending *pending =
      (cpkt_sasl_server_pending *)pthread_getspecific(
          cpkt_sasl_server_pending_key);
  const cpkt_sasl_server_plugin *public_plugins = NULL;
  cpkt_sasl_server_registration *reg;
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
      native_utils, 1, cpkt_sasl_global_option_application_context(1));
  status = pending->initialize(pending->context, utils, max_version,
                               version_out, &public_plugins, &count);
  if (status != SASL_OK && status != SASL_CONTINUE && status != SASL_NOUSER)
    return status;
  if (*version_out != SASL_SERVER_PLUG_VERSION || count < 0 ||
      (count != 0 && public_plugins == NULL)) {
    cpkt_sasl_server_release_public(public_plugins, count, utils);
    return SASL_BADVERS;
  }
  if (count == 0)
    return status;
  reg = (cpkt_sasl_server_registration *)calloc(1, sizeof(*reg));
  if (reg == NULL) {
    cpkt_sasl_server_release_public(public_plugins, count, utils);
    return SASL_NOMEM;
  }
  reg->records =
      (cpkt_sasl_server_record *)calloc((size_t)count, sizeof(*reg->records));
  reg->native_plugins =
      (sasl_server_plug_t *)calloc((size_t)count, sizeof(*reg->native_plugins));
  if (reg->records == NULL || reg->native_plugins == NULL) {
    cpkt_sasl_server_release_public(public_plugins, count, utils);
    free(reg->native_plugins);
    free(reg->records);
    free(reg);
    return SASL_NOMEM;
  }
  reg->count = count;
  reg->utils = utils;
  for (i = 0; i < count; ++i) {
    cpkt_sasl_server_record *record = &reg->records[i];
    sasl_server_plug_t *native = &reg->native_plugins[i];
    record->public_plugin = public_plugins[i];
    record->registration = reg;
    if (record->public_plugin.mechanism_name == NULL ||
        record->public_plugin.new_connection == NULL ||
        record->public_plugin.step == NULL ||
        record->public_plugin.maximum_ssf > UINT_MAX ||
        record->public_plugin.security_flags > UINT_MAX ||
        record->public_plugin.features > UINT_MAX) {
      cpkt_sasl_server_release_public(public_plugins, count, utils);
      free(reg->native_plugins);
      free(reg->records);
      free(reg);
      return SASL_BADPARAM;
    }
    native->mech_name = record->public_plugin.mechanism_name;
    native->max_ssf = (sasl_ssf_t)record->public_plugin.maximum_ssf;
    native->security_flags = (unsigned)record->public_plugin.security_flags;
    native->features = (unsigned)record->public_plugin.features;
    native->glob_context = record;
    native->mech_new = cpkt_sasl_server_new_native;
    native->mech_step = cpkt_sasl_server_step_native;
    native->mech_dispose = cpkt_sasl_server_dispose_native;
    native->mech_free = cpkt_sasl_server_free_native;
    native->setpass = record->public_plugin.set_password == NULL
                          ? NULL
                          : cpkt_sasl_server_setpass_native;
    native->user_query = record->public_plugin.user_query == NULL
                             ? NULL
                             : cpkt_sasl_server_user_query_native;
    native->idle = record->public_plugin.idle == NULL
                       ? NULL
                       : cpkt_sasl_server_idle_native;
    native->mech_avail = record->public_plugin.available == NULL
                             ? NULL
                             : cpkt_sasl_server_avail_native;
  }
  pending->registration = reg;
  *plugins_out = reg->native_plugins;
  *count_out = count;
  return status;
}

/** C89 facade contract for cpkt_sasl_server_add_plugin; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_server_add_plugin(const char *name,
                                cpkt_sasl_server_plugin_init initialize,
                                void *context) {
  cpkt_sasl_server_pending *pending, *previous;
  int status, provider_status, i;
  if (name == NULL || initialize == NULL)
    return SASL_BADPARAM;
  if (pthread_once(&cpkt_sasl_server_pending_once, cpkt_sasl_server_make_key) !=
          0 ||
      cpkt_sasl_server_pending_key_status != 0)
    return SASL_FAIL;
  pending = (cpkt_sasl_server_pending *)calloc(1, sizeof(*pending));
  if (pending == NULL)
    return SASL_NOMEM;
  previous = (cpkt_sasl_server_pending *)pthread_getspecific(
      cpkt_sasl_server_pending_key);
  pending->initialize = initialize;
  pending->context = context;
  if (pthread_setspecific(cpkt_sasl_server_pending_key, pending) != 0) {
    free(pending);
    return SASL_FAIL;
  }
  provider_status = sasl_server_add_plugin(name, cpkt_sasl_server_init_native);
  if (pending->registration != NULL && provider_status != SASL_OK) {
    cpkt_sasl_server_registration *reg = pending->registration;
    (void)sasl_server_plugin_info(NULL, cpkt_sasl_server_adoption_observer,
                                  reg);
    for (i = 0; i < reg->count; ++i)
      if (!reg->records[i].adopted)
        cpkt_sasl_server_release_record(&reg->records[i]);
  }
  status = provider_status;
  if (pthread_setspecific(cpkt_sasl_server_pending_key, previous) != 0)
    status = SASL_FAIL;
  if (pending->registration != NULL) {
    (void)pthread_mutex_lock(&cpkt_sasl_server_lock);
    pending->registration->next = cpkt_sasl_server_registrations;
    cpkt_sasl_server_registrations = pending->registration;
    (void)pthread_mutex_unlock(&cpkt_sasl_server_lock);
  }
  if (pthread_getspecific(cpkt_sasl_server_pending_key) != pending)
    free(pending);
  return status;
}

__attribute__((visibility("hidden"))) void
cpkt_sasl_server_plugins_cleanup(void) {
  cpkt_sasl_server_registration *reg, *next;
  (void)pthread_mutex_lock(&cpkt_sasl_server_lock);
  reg = cpkt_sasl_server_registrations;
  cpkt_sasl_server_registrations = NULL;
  (void)pthread_mutex_unlock(&cpkt_sasl_server_lock);
  while (reg != NULL) {
    next = reg->next;
    free(reg->native_plugins);
    free(reg->records);
    free(reg);
    reg = next;
  }
}

const cpkt_sasl_server_plugin *
cpkt_sasl_server_plugin_for_native(const sasl_server_plug_t *native) {
  cpkt_sasl_server_registration *reg;
  const cpkt_sasl_server_plugin *result = NULL;
  int i;
  (void)pthread_mutex_lock(&cpkt_sasl_server_lock);
  for (reg = cpkt_sasl_server_registrations; reg != NULL; reg = reg->next) {
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
  (void)pthread_mutex_unlock(&cpkt_sasl_server_lock);
  return result;
}
