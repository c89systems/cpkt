#include <cpkt/sasl_plugin.h>

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <sasl/sasl.h>
#include <sasl/saslplug.h>

#include "sasl_private.h"

typedef struct cpkt_sasl_canon_registration {
  cpkt_sasl_canonicalizer_plugin public_plugin;
  sasl_canonuser_plug_t native_plugin;
  char *name;
  int freed;
  cpkt_sasl_plugin_utils *utils;
  struct cpkt_sasl_canon_registration *next;
} cpkt_sasl_canon_registration;

typedef struct cpkt_sasl_canon_pending {
  cpkt_sasl_canonicalizer_plugin_init initialize;
  void *context;
  cpkt_sasl_canon_registration *registration;
} cpkt_sasl_canon_pending;

static pthread_key_t cpkt_sasl_canon_pending_key;
static pthread_once_t cpkt_sasl_canon_pending_once = PTHREAD_ONCE_INIT;
static int cpkt_sasl_canon_pending_key_status;
static pthread_mutex_t cpkt_sasl_canon_lock = PTHREAD_MUTEX_INITIALIZER;
static cpkt_sasl_canon_registration *cpkt_sasl_canon_registrations;

static void cpkt_sasl_canon_make_key(void) {
  cpkt_sasl_canon_pending_key_status =
      pthread_key_create(&cpkt_sasl_canon_pending_key, free);
}

static char *cpkt_sasl_canon_copy_name(const char *name) {
  size_t length;
  char *copy;
  if (name == NULL)
    return NULL;
  length = strlen(name);
  copy = (char *)malloc(length + 1U);
  if (copy != NULL)
    memcpy(copy, name, length + 1U);
  return copy;
}

static void cpkt_sasl_canon_free_native(void *context,
                                        const sasl_utils_t *native_utils) {
  cpkt_sasl_canon_registration *reg = (cpkt_sasl_canon_registration *)context;
  if (reg == NULL || reg->freed)
    return;
  (void)native_utils;
  if (reg->public_plugin.free_context != NULL)
    reg->public_plugin.free_context(reg->public_plugin.context, reg->utils);
  reg->freed = 1;
}

static int cpkt_sasl_canon_server_native(void *context,
                                         sasl_server_params_t *native_params,
                                         const char *user, unsigned length,
                                         unsigned flags, char *output,
                                         unsigned capacity,
                                         unsigned *length_out) {
  cpkt_sasl_canon_registration *reg = (cpkt_sasl_canon_registration *)context;
  cpkt_sasl_plugin_server_view view;
  unsigned long public_length = 0;
  int status;
  if (reg == NULL || native_params == NULL || reg->public_plugin.server == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_plugin_server_params_from_native(&view, native_params);
  if (view.params.utils == NULL)
    return SASL_NOMEM;
  status = reg->public_plugin.server(reg->public_plugin.context, &view.params,
                                     user, length, flags, output, capacity,
                                     &public_length);
  cpkt_sasl_property_discard_borrowed_views(&view.properties);
  if (public_length > UINT_MAX)
    return SASL_BADPARAM;
  if (length_out != NULL)
    *length_out = (unsigned)public_length;
  return status;
}

static int cpkt_sasl_canon_client_native(void *context,
                                         sasl_client_params_t *native_params,
                                         const char *user, unsigned length,
                                         unsigned flags, char *output,
                                         unsigned capacity,
                                         unsigned *length_out) {
  cpkt_sasl_canon_registration *reg = (cpkt_sasl_canon_registration *)context;
  cpkt_sasl_plugin_client_view view;
  unsigned long public_length = 0;
  int status;
  if (reg == NULL || native_params == NULL || reg->public_plugin.client == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_plugin_client_params_from_native(&view, native_params);
  if (view.params.utils == NULL)
    return SASL_NOMEM;
  status = reg->public_plugin.client(reg->public_plugin.context, &view.params,
                                     user, length, flags, output, capacity,
                                     &public_length);
  if (public_length > UINT_MAX)
    return SASL_BADPARAM;
  if (length_out != NULL)
    *length_out = (unsigned)public_length;
  return status;
}

static int cpkt_sasl_canon_init_native(const sasl_utils_t *native_utils,
                                       int max_version, int *version_out,
                                       sasl_canonuser_plug_t **plugin_out,
                                       const char *name) {
  cpkt_sasl_canon_pending *pending;
  const cpkt_sasl_canonicalizer_plugin *public_plugin = NULL;
  cpkt_sasl_canon_registration *reg;
  cpkt_sasl_plugin_utils *utils;
  int status;
  pending = (cpkt_sasl_canon_pending *)pthread_getspecific(
      cpkt_sasl_canon_pending_key);
  if (pending == NULL || pending->initialize == NULL || plugin_out == NULL)
    return SASL_BADPARAM;
  *plugin_out = NULL;
  utils = cpkt_sasl_plugin_utils_for_native(native_utils);
  if (utils == NULL)
    return SASL_NOMEM;
  status = pending->initialize(pending->context, utils, max_version,
                               version_out, &public_plugin, name);
  if (status != SASL_OK)
    return status;
  if (public_plugin == NULL || *version_out != SASL_CANONUSER_PLUG_VERSION) {
    if (public_plugin != NULL && public_plugin->free_context != NULL)
      public_plugin->free_context(public_plugin->context, utils);
    return SASL_BADVERS;
  }
  reg = (cpkt_sasl_canon_registration *)calloc(1, sizeof(*reg));
  if (reg == NULL) {
    if (public_plugin->free_context != NULL)
      public_plugin->free_context(public_plugin->context, utils);
    return SASL_NOMEM;
  }
  reg->public_plugin = *public_plugin;
  reg->utils = utils;
  reg->name = cpkt_sasl_canon_copy_name(
      public_plugin->name == NULL ? name : public_plugin->name);
  if (reg->name == NULL) {
    if (reg->public_plugin.free_context != NULL)
      reg->public_plugin.free_context(reg->public_plugin.context, utils);
    free(reg);
    return SASL_NOMEM;
  }
  reg->native_plugin.features = public_plugin->features;
  reg->native_plugin.spare_int1 = 0;
  reg->native_plugin.glob_context = reg;
  reg->native_plugin.name = reg->name;
  reg->native_plugin.canon_user_free = cpkt_sasl_canon_free_native;
  reg->native_plugin.canon_user_server =
      public_plugin->server == NULL ? NULL : cpkt_sasl_canon_server_native;
  reg->native_plugin.canon_user_client =
      public_plugin->client == NULL ? NULL : cpkt_sasl_canon_client_native;
  pending->registration = reg;
  *plugin_out = &reg->native_plugin;
  return SASL_OK;
}

/** C89 facade contract for cpkt_sasl_canonicalizer_add_plugin; see the public
 * header for ownership and callback lifetime. */
int cpkt_sasl_canonicalizer_add_plugin(
    const char *name, cpkt_sasl_canonicalizer_plugin_init initialize,
    void *context) {
  cpkt_sasl_canon_pending *pending;
  cpkt_sasl_canon_pending *previous;
  int status, provider_status;
  if (name == NULL || initialize == NULL)
    return SASL_BADPARAM;
  if (pthread_once(&cpkt_sasl_canon_pending_once, cpkt_sasl_canon_make_key) !=
          0 ||
      cpkt_sasl_canon_pending_key_status != 0)
    return SASL_FAIL;
  pending = (cpkt_sasl_canon_pending *)calloc(1, sizeof(*pending));
  if (pending == NULL)
    return SASL_NOMEM;
  previous = (cpkt_sasl_canon_pending *)pthread_getspecific(
      cpkt_sasl_canon_pending_key);
  pending->initialize = initialize;
  pending->context = context;
  if (pthread_setspecific(cpkt_sasl_canon_pending_key, pending) != 0) {
    free(pending);
    return SASL_FAIL;
  }
  provider_status =
      sasl_canonuser_add_plugin(name, cpkt_sasl_canon_init_native);
  if (provider_status != SASL_OK && pending->registration != NULL) {
    cpkt_sasl_canon_registration *reg = pending->registration;
    if (reg->public_plugin.free_context != NULL)
      reg->public_plugin.free_context(reg->public_plugin.context, reg->utils);
    reg->freed = 1;
  }
  status = provider_status;
  if (pthread_setspecific(cpkt_sasl_canon_pending_key, previous) != 0)
    status = SASL_FAIL;
  if (pending->registration != NULL) {
    (void)pthread_mutex_lock(&cpkt_sasl_canon_lock);
    pending->registration->next = cpkt_sasl_canon_registrations;
    cpkt_sasl_canon_registrations = pending->registration;
    (void)pthread_mutex_unlock(&cpkt_sasl_canon_lock);
  }
  if (pthread_getspecific(cpkt_sasl_canon_pending_key) != pending)
    free(pending);
  return status;
}

__attribute__((visibility("hidden"))) void
cpkt_sasl_canonicalizers_cleanup(void) {
  cpkt_sasl_canon_registration *reg, *next;
  (void)pthread_mutex_lock(&cpkt_sasl_canon_lock);
  reg = cpkt_sasl_canon_registrations;
  cpkt_sasl_canon_registrations = NULL;
  (void)pthread_mutex_unlock(&cpkt_sasl_canon_lock);
  while (reg != NULL) {
    next = reg->next;
    free(reg->name);
    free(reg);
    reg = next;
  }
}
