#include <cpkt/sasl_plugin.h>

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <sasl/sasl.h>
#include <sasl/saslplug.h>

#include "sasl_private.h"

typedef struct cpkt_sasl_aux_registration {
  cpkt_sasl_auxiliary_plugin public_plugin;
  sasl_auxprop_plug_t native_plugin;
  char *name;
  int freed;
  cpkt_sasl_plugin_utils *utils;
  struct cpkt_sasl_aux_registration *next;
} cpkt_sasl_aux_registration;

typedef struct cpkt_sasl_aux_pending {
  cpkt_sasl_auxiliary_plugin_init initialize;
  void *context;
  cpkt_sasl_aux_registration *registration;
} cpkt_sasl_aux_pending;

static pthread_key_t cpkt_sasl_aux_pending_key;
static pthread_once_t cpkt_sasl_aux_pending_once = PTHREAD_ONCE_INIT;
static int cpkt_sasl_aux_pending_key_status;
static cpkt_sasl_aux_registration *cpkt_sasl_aux_registrations;
static pthread_mutex_t cpkt_sasl_aux_lock = PTHREAD_MUTEX_INITIALIZER;

static void cpkt_sasl_aux_make_key(void) {
  cpkt_sasl_aux_pending_key_status =
      pthread_key_create(&cpkt_sasl_aux_pending_key, free);
}

static char *cpkt_sasl_aux_copy_name(const char *name) {
  char *copy;
  size_t length;
  if (name == NULL)
    return NULL;
  length = strlen(name);
  copy = (char *)malloc(length + 1U);
  if (copy != NULL)
    memcpy(copy, name, length + 1U);
  return copy;
}

/** C89 facade contract for cpkt_sasl_plugin_server_params_from_native; see the
 * public header for ownership and callback lifetime. */
void cpkt_sasl_plugin_server_params_from_native(
    cpkt_sasl_plugin_server_view *view, sasl_server_params_t *native) {
  cpkt_sasl_server_params *out = &view->params;
  memset(view, 0, sizeof(*view));
  out->service = native->service;
  out->application_name = native->appname;
  out->server_name = native->serverFQDN;
  out->user_realm = native->user_realm;
  out->local_endpoint = native->iplocalport;
  out->remote_endpoint = native->ipremoteport;
  if (native->callbacks != NULL && native->callbacks[0].id != SASL_CB_LIST_END)
    out->callbacks =
        cpkt_sasl_callbacks_from_native_context(native->callbacks[0].context);
  out->service_length = native->servicelen;
  out->application_length = native->applen;
  out->server_length = native->slen;
  out->realm_length = native->urlen;
  out->local_length = native->iploclen;
  out->remote_length = native->ipremlen;
  out->log_level = native->log_level;
  out->security.minimum_ssf = native->props.min_ssf;
  out->security.maximum_ssf = native->props.max_ssf;
  out->security.maximum_buffer_bytes = native->props.maxbufsize;
  out->security.security_flags = native->props.security_flags;
  out->security.property_names = native->props.property_names;
  out->security.property_values = native->props.property_values;
  out->external_ssf = native->external_ssf;
  view->properties.native = native->propctx;
  view->properties.borrowed = 1;
  out->properties = native->propctx == NULL ? NULL : &view->properties;
  out->gss_credentials = (const cpkt_gss_credential *)native->gss_creds;
  if (native->cbinding != NULL) {
    view->binding.name = native->cbinding->name;
    view->binding.critical = native->cbinding->critical;
    view->binding.byte_count = native->cbinding->len;
    view->binding.data = native->cbinding->data;
    out->channel_binding = &view->binding;
  }
  if (native->http_request != NULL) {
    view->request.method = native->http_request->method;
    view->request.uri = native->http_request->uri;
    view->request.entity = native->http_request->entity;
    view->request.entity_byte_count = native->http_request->elen;
    view->request.non_persistent = native->http_request->non_persist;
    out->http_request = &view->request;
  }
  out->flags = native->flags;
  out->parameter_version = native->param_version;
  out->transition =
      native->transition == NULL ? NULL : cpkt_sasl_plugin_server_transition;
  out->canonicalize =
      native->canon_user == NULL ? NULL : cpkt_sasl_plugin_server_canonicalize;
  out->utils = cpkt_sasl_plugin_utils_for_native(native->utils);
  out->internal = native;
}

static void cpkt_sasl_aux_free_native(void *context,
                                      const sasl_utils_t *native_utils) {
  cpkt_sasl_aux_registration *reg = (cpkt_sasl_aux_registration *)context;
  if (reg == NULL || reg->freed)
    return;
  (void)native_utils;
  if (reg->public_plugin.free_context != NULL)
    reg->public_plugin.free_context(reg->public_plugin.context, reg->utils);
  reg->freed = 1;
}

static int cpkt_sasl_aux_lookup_native(void *context,
                                       sasl_server_params_t *native_params,
                                       unsigned flags, const char *user,
                                       unsigned user_length) {
  cpkt_sasl_aux_registration *reg = (cpkt_sasl_aux_registration *)context;
  cpkt_sasl_plugin_server_view view;
  int status;
  if (reg == NULL || reg->public_plugin.lookup == NULL || native_params == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_plugin_server_params_from_native(&view, native_params);
  if (view.params.utils == NULL)
    return SASL_NOMEM;
  status = reg->public_plugin.lookup(reg->public_plugin.context, &view.params,
                                     flags, user, user_length);
  cpkt_sasl_property_discard_borrowed_views(&view.properties);
  return status;
}

static int cpkt_sasl_aux_store_native(void *context,
                                      sasl_server_params_t *native_params,
                                      struct propctx *native_properties,
                                      const char *user, unsigned user_length) {
  cpkt_sasl_aux_registration *reg = (cpkt_sasl_aux_registration *)context;
  cpkt_sasl_plugin_server_view view;
  cpkt_sasl_property_context properties;
  int status;
  if (reg == NULL || reg->public_plugin.store == NULL || native_params == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_plugin_server_params_from_native(&view, native_params);
  if (view.params.utils == NULL)
    return SASL_NOMEM;
  memset(&properties, 0, sizeof(properties));
  properties.native = native_properties;
  properties.borrowed = 1;
  status = reg->public_plugin.store(
      reg->public_plugin.context, &view.params,
      native_properties == NULL ? NULL : &properties, user, user_length);
  cpkt_sasl_property_discard_borrowed_views(&properties);
  cpkt_sasl_property_discard_borrowed_views(&view.properties);
  return status;
}

static int cpkt_sasl_aux_init_native(const sasl_utils_t *native_utils,
                                     int max_version, int *version_out,
                                     sasl_auxprop_plug_t **plugin_out,
                                     const char *name) {
  cpkt_sasl_aux_pending *pending;
  const cpkt_sasl_auxiliary_plugin *public_plugin = NULL;
  cpkt_sasl_aux_registration *reg;
  cpkt_sasl_plugin_utils *utils;
  int status;
  pending =
      (cpkt_sasl_aux_pending *)pthread_getspecific(cpkt_sasl_aux_pending_key);
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
  if (public_plugin == NULL || public_plugin->lookup == NULL ||
      *version_out != SASL_AUXPROP_PLUG_VERSION) {
    if (public_plugin != NULL && public_plugin->free_context != NULL)
      public_plugin->free_context(public_plugin->context, utils);
    return SASL_BADVERS;
  }
  reg = (cpkt_sasl_aux_registration *)calloc(1, sizeof(*reg));
  if (reg == NULL) {
    if (public_plugin->free_context != NULL)
      public_plugin->free_context(public_plugin->context, utils);
    return SASL_NOMEM;
  }
  reg->public_plugin = *public_plugin;
  reg->utils = utils;
  reg->name = cpkt_sasl_aux_copy_name(
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
  reg->native_plugin.auxprop_free = cpkt_sasl_aux_free_native;
  reg->native_plugin.auxprop_lookup = cpkt_sasl_aux_lookup_native;
  reg->native_plugin.name = reg->name;
  reg->native_plugin.auxprop_store =
      public_plugin->store == NULL ? NULL : cpkt_sasl_aux_store_native;
  pending->registration = reg;
  *plugin_out = &reg->native_plugin;
  return SASL_OK;
}

/** C89 facade contract for cpkt_sasl_auxiliary_add_plugin; see the public
 * header for ownership and callback lifetime. */
int cpkt_sasl_auxiliary_add_plugin(const char *name,
                                   cpkt_sasl_auxiliary_plugin_init initialize,
                                   void *context) {
  cpkt_sasl_aux_pending *pending;
  cpkt_sasl_aux_pending *previous;
  int status, provider_status;
  if (name == NULL || initialize == NULL)
    return SASL_BADPARAM;
  if (pthread_once(&cpkt_sasl_aux_pending_once, cpkt_sasl_aux_make_key) != 0 ||
      cpkt_sasl_aux_pending_key_status != 0)
    return SASL_FAIL;
  pending = (cpkt_sasl_aux_pending *)calloc(1, sizeof(*pending));
  if (pending == NULL)
    return SASL_NOMEM;
  previous =
      (cpkt_sasl_aux_pending *)pthread_getspecific(cpkt_sasl_aux_pending_key);
  pending->initialize = initialize;
  pending->context = context;
  if (pthread_setspecific(cpkt_sasl_aux_pending_key, pending) != 0) {
    free(pending);
    return SASL_FAIL;
  }
  provider_status = sasl_auxprop_add_plugin(name, cpkt_sasl_aux_init_native);
  if (provider_status != SASL_OK && pending->registration != NULL) {
    cpkt_sasl_aux_registration *reg = pending->registration;
    if (reg->public_plugin.free_context != NULL)
      reg->public_plugin.free_context(reg->public_plugin.context, reg->utils);
    reg->freed = 1;
  }
  status = provider_status;
  if (pthread_setspecific(cpkt_sasl_aux_pending_key, previous) != 0)
    status = SASL_FAIL;
  if (pending->registration != NULL) {
    (void)pthread_mutex_lock(&cpkt_sasl_aux_lock);
    pending->registration->next = cpkt_sasl_aux_registrations;
    cpkt_sasl_aux_registrations = pending->registration;
    (void)pthread_mutex_unlock(&cpkt_sasl_aux_lock);
  }
  if (pthread_getspecific(cpkt_sasl_aux_pending_key) != pending)
    free(pending);
  return status;
}

/* Called only after native server shutdown has invoked auxprop_free. */
__attribute__((visibility("hidden"))) void
cpkt_sasl_auxiliary_plugins_cleanup(void) {
  cpkt_sasl_aux_registration *reg, *next;
  (void)pthread_mutex_lock(&cpkt_sasl_aux_lock);
  reg = cpkt_sasl_aux_registrations;
  cpkt_sasl_aux_registrations = NULL;
  (void)pthread_mutex_unlock(&cpkt_sasl_aux_lock);
  while (reg != NULL) {
    next = reg->next;
    free(reg->name);
    free(reg);
    reg = next;
  }
}

const cpkt_sasl_auxiliary_plugin *
cpkt_sasl_auxiliary_plugin_for_native(const sasl_auxprop_plug_t *native) {
  cpkt_sasl_aux_registration *reg;
  const cpkt_sasl_auxiliary_plugin *result = NULL;
  (void)pthread_mutex_lock(&cpkt_sasl_aux_lock);
  for (reg = cpkt_sasl_aux_registrations; reg != NULL; reg = reg->next) {
    if (native == &reg->native_plugin ||
        (native != NULL && native->glob_context == reg)) {
      result = &reg->public_plugin;
      break;
    }
  }
  (void)pthread_mutex_unlock(&cpkt_sasl_aux_lock);
  return result;
}
