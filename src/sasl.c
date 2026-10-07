#include <cpkt/sasl.h>

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

#include <gssapi/gssapi.h>
#include <sasl/sasl.h>
#include <sasl/saslutil.h>

#include "sasl_private.h"

typedef char cpkt_sasl_unsigned_fits_public_type
    [(sizeof(unsigned) <= sizeof(unsigned long)) ? 1 : -1];

typedef struct cpkt_sasl_callback_owner {
  cpkt_sasl_callbacks callbacks;
  cpkt_sasl *public_receiver;
} cpkt_sasl_callback_owner;

typedef struct cpkt_sasl_state {
  sasl_conn_t *native;
  sasl_callback_t native_callbacks[18];
  cpkt_sasl_callback_owner callback_owner;
  sasl_interact_t *pending_native_interactions;
  cpkt_sasl_interaction *pending_public_interactions;
  unsigned long pending_interaction_count;
  sasl_secret_t *native_secret;
  size_t native_secret_size;
  int is_server;
  sasl_channel_binding_t channel_binding;
  sasl_http_request_t http_request;
  cpkt_sasl_property_context auxiliary_context;
  struct cpkt_sasl_state *next;
} cpkt_sasl_state;

typedef struct cpkt_sasl_global_callbacks {
  cpkt_sasl_callback_owner callback_owner;
  sasl_callback_t native[18];
  unsigned long references;
} cpkt_sasl_global_callbacks;

/* Cyrus SASL intentionally stores heterogeneous callback signatures in one
 * generic function-pointer slot.  Keep that ABI bridge private and explicit. */
typedef union cpkt_sasl_native_callback {
  int (*generic)(void);
  sasl_getopt_t *option;
  sasl_log_t *log;
  sasl_getpath_t *path;
  sasl_verifyfile_t *verify;
  sasl_getconfpath_t *configuration_path;
  sasl_getsimple_t *simple;
  sasl_getsecret_t *secret;
  sasl_chalprompt_t *challenge;
  sasl_getrealm_t *realm;
  sasl_authorize_t *authorize;
  sasl_server_userdb_checkpass_t *check_password;
  sasl_server_userdb_setpass_t *set_password;
  sasl_canon_user_t *canonicalize;
} cpkt_sasl_native_callback;

static cpkt_sasl_global_callbacks cpkt_sasl_client_callbacks;
static cpkt_sasl_global_callbacks cpkt_sasl_server_callbacks;
static pthread_mutex_t cpkt_sasl_receiver_lock = PTHREAD_MUTEX_INITIALIZER;
static cpkt_sasl_state *cpkt_sasl_receivers;

/** C89 facade contract for cpkt_sasl_public_for_native; see the public header
 * for ownership and callback lifetime. */
cpkt_sasl *cpkt_sasl_public_for_native(sasl_conn_t *native) {
  cpkt_sasl_state *state;
  cpkt_sasl *receiver = NULL;
  if (native == NULL)
    return NULL;
  (void)pthread_mutex_lock(&cpkt_sasl_receiver_lock);
  for (state = cpkt_sasl_receivers; state != NULL; state = state->next) {
    if (state->native == native) {
      receiver = state->callback_owner.public_receiver;
      break;
    }
  }
  (void)pthread_mutex_unlock(&cpkt_sasl_receiver_lock);
  return receiver;
}

const cpkt_sasl_callbacks *
cpkt_sasl_callbacks_from_native_context(void *context) {
  cpkt_sasl_state *state;
  const cpkt_sasl_callbacks *callbacks = NULL;
  if (context == &cpkt_sasl_client_callbacks.callback_owner)
    return &cpkt_sasl_client_callbacks.callback_owner.callbacks;
  if (context == &cpkt_sasl_server_callbacks.callback_owner)
    return &cpkt_sasl_server_callbacks.callback_owner.callbacks;
  (void)pthread_mutex_lock(&cpkt_sasl_receiver_lock);
  for (state = cpkt_sasl_receivers; state != NULL; state = state->next) {
    if (context == &state->callback_owner) {
      callbacks = &state->callback_owner.callbacks;
      break;
    }
  }
  (void)pthread_mutex_unlock(&cpkt_sasl_receiver_lock);
  return callbacks;
}

static void cpkt_sasl_register_receiver(cpkt_sasl_state *state) {
  (void)pthread_mutex_lock(&cpkt_sasl_receiver_lock);
  state->next = cpkt_sasl_receivers;
  cpkt_sasl_receivers = state;
  (void)pthread_mutex_unlock(&cpkt_sasl_receiver_lock);
}

static void cpkt_sasl_unregister_receiver(cpkt_sasl_state *state) {
  cpkt_sasl_state **link;
  (void)pthread_mutex_lock(&cpkt_sasl_receiver_lock);
  for (link = &cpkt_sasl_receivers; *link != NULL; link = &(*link)->next) {
    if (*link == state) {
      *link = state->next;
      break;
    }
  }
  (void)pthread_mutex_unlock(&cpkt_sasl_receiver_lock);
}

static cpkt_sasl_state *cpkt_sasl_state_for(const cpkt_sasl *self) {
  return self == NULL ? NULL : (cpkt_sasl_state *)self->internal;
}

/** Resolves the native connection for private property adapters. A closed or
 * null receiver has no native connection. */
sasl_conn_t *cpkt_sasl_native_connection(const cpkt_sasl *self) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  return state == NULL ? NULL : state->native;
}

/** Returns the local option callback context, or the role's global fallback
 * context. NULL means no application option callback is configured. */
void *cpkt_sasl_option_application_context(const cpkt_sasl *self) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  const cpkt_sasl_callbacks *global;
  if (state == NULL)
    return NULL;
  if (state->callback_owner.callbacks.option != NULL)
    return state->callback_owner.callbacks.context;
  global = state->is_server
               ? &cpkt_sasl_server_callbacks.callback_owner.callbacks
               : &cpkt_sasl_client_callbacks.callback_owner.callbacks;
  return global->option == NULL ? NULL : global->context;
}

/** Resolves the application context retained by a role's global option
 * callback while its native plugin initialization is in progress. */
void *cpkt_sasl_global_option_application_context(int is_server) {
  const cpkt_sasl_callbacks *global =
      is_server ? &cpkt_sasl_server_callbacks.callback_owner.callbacks
                : &cpkt_sasl_client_callbacks.callback_owner.callbacks;
  return global->option == NULL ? NULL : global->context;
}

static void cpkt_sasl_clear_native_secret(cpkt_sasl_state *state) {
  volatile unsigned char *bytes;
  size_t index;
  if (state == NULL || state->native_secret == NULL)
    return;
  bytes = (volatile unsigned char *)state->native_secret;
  for (index = 0; index < state->native_secret_size; ++index)
    bytes[index] = 0;
  free(state->native_secret);
  state->native_secret = NULL;
  state->native_secret_size = 0;
}

static void cpkt_sasl_clear_pending_interactions(cpkt_sasl_state *state) {
  if (state == NULL)
    return;
  free(state->pending_public_interactions);
  state->pending_native_interactions = NULL;
  state->pending_public_interactions = NULL;
  state->pending_interaction_count = 0;
}

static int
cpkt_sasl_store_pending_interactions(cpkt_sasl_state *state,
                                     sasl_interact_t *native_interactions,
                                     cpkt_sasl_interaction **interactions) {
  cpkt_sasl_interaction *public_interactions;
  unsigned long count;
  unsigned long index;
  if (interactions != NULL)
    *interactions = NULL;
  cpkt_sasl_clear_pending_interactions(state);
  if (native_interactions == NULL)
    return SASL_INTERACT;
  count = 0;
  while (native_interactions[count].id != SASL_CB_LIST_END) {
    if (count == (unsigned long)-1 ||
        (size_t)count > ((size_t)-1) / sizeof(*public_interactions) - 1U) {
      return SASL_NOMEM;
    }
    ++count;
  }
  public_interactions = (cpkt_sasl_interaction *)calloc(
      (size_t)(count + 1U), sizeof(*public_interactions));
  if (public_interactions == NULL)
    return SASL_NOMEM;
  for (index = 0; index < count; ++index) {
    public_interactions[index].id = native_interactions[index].id;
    public_interactions[index].challenge = native_interactions[index].challenge;
    public_interactions[index].prompt = native_interactions[index].prompt;
    public_interactions[index].default_result =
        native_interactions[index].defresult;
    public_interactions[index].result = native_interactions[index].result;
    public_interactions[index].result_byte_count =
        (unsigned long)native_interactions[index].len;
  }
  public_interactions[count].id = SASL_CB_LIST_END;
  state->pending_native_interactions = native_interactions;
  state->pending_public_interactions = public_interactions;
  state->pending_interaction_count = count;
  if (interactions != NULL)
    *interactions = public_interactions;
  return SASL_INTERACT;
}

static int
cpkt_sasl_prepare_pending_interactions(cpkt_sasl_state *state,
                                       cpkt_sasl_interaction **interactions,
                                       sasl_interact_t **native_interactions) {
  unsigned long index;
  if (native_interactions != NULL)
    *native_interactions = NULL;
  if (state == NULL)
    return SASL_BADPARAM;
  if (state->pending_native_interactions == NULL) {
    return interactions != NULL && *interactions != NULL ? SASL_BADPARAM
                                                         : SASL_OK;
  }
  if (interactions == NULL ||
      *interactions != state->pending_public_interactions) {
    return SASL_BADPARAM;
  }
  for (index = 0; index < state->pending_interaction_count; ++index) {
    if (state->pending_public_interactions[index].result_byte_count >
        UINT_MAX) {
      return SASL_BADPARAM;
    }
    state->pending_native_interactions[index].result =
        state->pending_public_interactions[index].result;
    state->pending_native_interactions[index].len =
        (unsigned)state->pending_public_interactions[index].result_byte_count;
  }
  if (native_interactions != NULL)
    *native_interactions = state->pending_native_interactions;
  return SASL_OK;
}

static int cpkt_sasl_option_native(void *context, const char *plugin,
                                   const char *option, const char **result,
                                   unsigned *length) {
  cpkt_sasl_callback_owner *owner;
  unsigned long public_length;
  owner = (cpkt_sasl_callback_owner *)context;
  public_length = 0;
  if (owner == NULL || owner->callbacks.option == NULL)
    return SASL_FAIL;
  {
    int status;
    status = owner->callbacks.option(owner->callbacks.context, plugin, option,
                                     result, &public_length);
    if (public_length > UINT_MAX)
      return SASL_BADPARAM;
    if (length != NULL)
      *length = (unsigned)public_length;
    return status;
  }
}

static int cpkt_sasl_log_native(void *context, int level, const char *message) {
  cpkt_sasl_callback_owner *owner;
  owner = (cpkt_sasl_callback_owner *)context;
  return owner == NULL || owner->callbacks.log == NULL
             ? SASL_FAIL
             : owner->callbacks.log(owner->callbacks.context, level, message);
}

static int cpkt_sasl_path_native(void *context, const char **path) {
  cpkt_sasl_callback_owner *owner;
  owner = (cpkt_sasl_callback_owner *)context;
  return owner == NULL || owner->callbacks.plugin_path == NULL
             ? SASL_FAIL
             : owner->callbacks.plugin_path(owner->callbacks.context, path);
}

static int cpkt_sasl_verify_native(void *context, const char *path,
                                   sasl_verify_type_t type) {
  cpkt_sasl_callback_owner *owner;
  owner = (cpkt_sasl_callback_owner *)context;
  return owner == NULL || owner->callbacks.verify_file == NULL
             ? SASL_FAIL
             : owner->callbacks.verify_file(owner->callbacks.context, path,
                                            (int)type);
}

static int cpkt_sasl_configuration_path_native(void *context, char **path) {
  cpkt_sasl_callback_owner *owner;
  owner = (cpkt_sasl_callback_owner *)context;
  return owner == NULL || owner->callbacks.configuration_path == NULL
             ? SASL_FAIL
             : owner->callbacks.configuration_path(owner->callbacks.context,
                                                   path);
}

static int cpkt_sasl_simple_native(void *context, int id, const char **result,
                                   unsigned *length) {
  cpkt_sasl_callback_owner *owner;
  cpkt_sasl_simple_callback callback;
  unsigned long public_length;
  int status;
  owner = (cpkt_sasl_callback_owner *)context;
  if (owner == NULL)
    return SASL_FAIL;
  callback = id == SASL_CB_LANGUAGE
                 ? owner->callbacks.language
                 : (id == SASL_CB_CNONCE ? owner->callbacks.client_nonce
                                         : owner->callbacks.simple);
  if (callback == NULL)
    return SASL_FAIL;
  public_length = 0;
  status = callback(owner->callbacks.context, id, result, &public_length);
  if (public_length > UINT_MAX)
    return SASL_BADPARAM;
  if (length != NULL)
    *length = (unsigned)public_length;
  return status;
}

static int cpkt_sasl_secret_native(sasl_conn_t *native, void *context, int id,
                                   sasl_secret_t **secret) {
  cpkt_sasl_callback_owner *owner;
  cpkt_sasl_state *state;
  const cpkt_sasl_secret *public_secret;
  sasl_secret_t *native_secret;
  size_t byte_count;
  size_t allocation_size;
  cpkt_sasl *receiver;
  int status;
  owner = (cpkt_sasl_callback_owner *)context;
  if (secret != NULL)
    *secret = NULL;
  if (owner == NULL || owner->callbacks.secret == NULL || secret == NULL)
    return SASL_FAIL;
  receiver = owner->public_receiver == NULL
                 ? cpkt_sasl_public_for_native(native)
                 : owner->public_receiver;
  state = cpkt_sasl_state_for(receiver);
  if (state == NULL)
    return SASL_BADPARAM;
  public_secret = NULL;
  status = owner->callbacks.secret(receiver, owner->callbacks.context, id,
                                   &public_secret);
  cpkt_sasl_clear_native_secret(state);
  if (status != SASL_OK || public_secret == NULL)
    return status;
  if (public_secret->byte_count > 0U && public_secret->data == NULL)
    return SASL_BADPARAM;
  if (sizeof(size_t) < sizeof(unsigned long) &&
      public_secret->byte_count > (unsigned long)((size_t)-1))
    return SASL_BADPARAM;
  byte_count = (size_t)public_secret->byte_count;
  if (byte_count > (size_t)-1 - sizeof(*native_secret))
    return SASL_BADPARAM;
  allocation_size = sizeof(*native_secret) + byte_count;
  native_secret = (sasl_secret_t *)malloc(allocation_size);
  if (native_secret == NULL)
    return SASL_NOMEM;
  native_secret->len = public_secret->byte_count;
  if (byte_count > 0U)
    memcpy(native_secret->data, public_secret->data, byte_count);
  native_secret->data[byte_count] = 0;
  state->native_secret = native_secret;
  state->native_secret_size = allocation_size;
  *secret = native_secret;
  return SASL_OK;
}

static int cpkt_sasl_challenge_native(void *context, int id,
                                      const char *challenge, const char *prompt,
                                      const char *default_result,
                                      const char **result, unsigned *length) {
  cpkt_sasl_callback_owner *owner;
  cpkt_sasl_challenge_callback callback;
  unsigned long public_length;
  int status;
  owner = (cpkt_sasl_callback_owner *)context;
  if (owner == NULL)
    return SASL_FAIL;
  callback = id == SASL_CB_NOECHOPROMPT ? owner->callbacks.challenge_no_echo
                                        : owner->callbacks.challenge;
  if (callback == NULL)
    return SASL_FAIL;
  public_length = 0;
  status = callback(owner->callbacks.context, id, challenge, prompt,
                    default_result, result, &public_length);
  if (public_length > UINT_MAX)
    return SASL_BADPARAM;
  if (length != NULL)
    *length = (unsigned)public_length;
  return status;
}

static int cpkt_sasl_realm_native(void *context, int id, const char **available,
                                  const char **result) {
  cpkt_sasl_callback_owner *owner;
  owner = (cpkt_sasl_callback_owner *)context;
  return owner == NULL || owner->callbacks.realm == NULL
             ? SASL_FAIL
             : owner->callbacks.realm(owner->callbacks.context, id,
                                      (const char *const *)available, result);
}

static int cpkt_sasl_authorize_native(
    sasl_conn_t *native, void *context, const char *requested,
    unsigned requested_length, const char *identity, unsigned identity_length,
    const char *realm, unsigned realm_length, struct propctx *properties) {
  cpkt_sasl_callback_owner *owner;
  cpkt_sasl *receiver;
  (void)properties;
  owner = (cpkt_sasl_callback_owner *)context;
  receiver = owner == NULL ? NULL
                           : (owner->public_receiver == NULL
                                  ? cpkt_sasl_public_for_native(native)
                                  : owner->public_receiver);
  return owner == NULL || owner->callbacks.authorize == NULL
             ? SASL_FAIL
             : owner->callbacks.authorize(
                   receiver, owner->callbacks.context, requested,
                   (unsigned long)requested_length, identity,
                   (unsigned long)identity_length, realm,
                   (unsigned long)realm_length);
}

static int cpkt_sasl_check_password_native(sasl_conn_t *native, void *context,
                                           const char *user,
                                           const char *password,
                                           unsigned password_length,
                                           struct propctx *properties) {
  cpkt_sasl_callback_owner *owner;
  cpkt_sasl *receiver;
  (void)properties;
  owner = (cpkt_sasl_callback_owner *)context;
  receiver = owner == NULL ? NULL
                           : (owner->public_receiver == NULL
                                  ? cpkt_sasl_public_for_native(native)
                                  : owner->public_receiver);
  return owner == NULL || owner->callbacks.check_password == NULL
             ? SASL_FAIL
             : owner->callbacks.check_password(
                   receiver, owner->callbacks.context, user, password,
                   (unsigned long)password_length);
}

static int cpkt_sasl_set_password_native(sasl_conn_t *native, void *context,
                                         const char *user, const char *password,
                                         unsigned password_length,
                                         struct propctx *properties,
                                         unsigned flags) {
  cpkt_sasl_callback_owner *owner;
  cpkt_sasl *receiver;
  (void)properties;
  owner = (cpkt_sasl_callback_owner *)context;
  receiver = owner == NULL ? NULL
                           : (owner->public_receiver == NULL
                                  ? cpkt_sasl_public_for_native(native)
                                  : owner->public_receiver);
  return owner == NULL || owner->callbacks.set_password == NULL
             ? SASL_FAIL
             : owner->callbacks.set_password(
                   receiver, owner->callbacks.context, user, password,
                   (unsigned long)password_length, (unsigned long)flags);
}

static int cpkt_sasl_canonicalize_native(sasl_conn_t *native, void *context,
                                         const char *input,
                                         unsigned input_length, unsigned flags,
                                         const char *realm, char *output,
                                         unsigned output_capacity,
                                         unsigned *output_length) {
  cpkt_sasl_callback_owner *owner;
  cpkt_sasl *receiver;
  unsigned long public_length;
  int status;
  owner = (cpkt_sasl_callback_owner *)context;
  if (owner == NULL || owner->callbacks.canonicalize == NULL)
    return SASL_FAIL;
  receiver = owner->public_receiver == NULL
                 ? cpkt_sasl_public_for_native(native)
                 : owner->public_receiver;
  public_length = 0;
  status = owner->callbacks.canonicalize(
      receiver, owner->callbacks.context, input, (unsigned long)input_length,
      (unsigned long)flags, realm, output, (unsigned long)output_capacity,
      &public_length);
  if (public_length > UINT_MAX)
    return SASL_BADPARAM;
  if (output_length != NULL)
    *output_length = (unsigned)public_length;
  return status;
}

static void cpkt_sasl_callbacks_build(sasl_callback_t *native,
                                      cpkt_sasl_callbacks *stored,
                                      const cpkt_sasl_callbacks *source,
                                      void *context) {
  int count;
  cpkt_sasl_native_callback callback;
  if (source == NULL)
    memset(stored, 0, sizeof(*stored));
  else
    *stored = *source;
  memset(native, 0, sizeof(sasl_callback_t) * 18U);
  count = 0;
#define CPKT_SASL_ADD_CALLBACK(identifier, public_field, native_field)         \
  if (stored->public_field != NULL) {                                          \
    native[count].id = identifier;                                             \
    callback.native_field = cpkt_sasl_##native_field##_native;                 \
    native[count].proc = callback.generic;                                     \
    native[count].context = context;                                           \
    ++count;                                                                   \
  }
  CPKT_SASL_ADD_CALLBACK(SASL_CB_GETOPT, option, option)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_LOG, log, log)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_GETPATH, plugin_path, path)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_VERIFYFILE, verify_file, verify)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_GETCONFPATH, configuration_path,
                         configuration_path)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_USER, simple, simple)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_AUTHNAME, simple, simple)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_LANGUAGE, language, simple)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_CNONCE, client_nonce, simple)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_PASS, secret, secret)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_ECHOPROMPT, challenge, challenge)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_NOECHOPROMPT, challenge_no_echo, challenge)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_GETREALM, realm, realm)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_PROXY_POLICY, authorize, authorize)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_SERVER_USERDB_CHECKPASS, check_password,
                         check_password)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_SERVER_USERDB_SETPASS, set_password,
                         set_password)
  CPKT_SASL_ADD_CALLBACK(SASL_CB_CANON_USER, canonicalize, canonicalize)
#undef CPKT_SASL_ADD_CALLBACK
  native[count].id = SASL_CB_LIST_END;
  native[count].proc = NULL;
  native[count].context = NULL;
}

static int cpkt_sasl_start(cpkt_sasl *self, const char *mechanisms,
                           cpkt_sasl_interaction **interactions,
                           const char **output, unsigned long *output_length,
                           const char **mechanism) {
  cpkt_sasl_state *state;
  unsigned native_length;
  state = cpkt_sasl_state_for(self);
  if (output != NULL)
    *output = NULL;
  if (output_length != NULL)
    *output_length = 0;
  if (mechanism != NULL)
    *mechanism = NULL;
  if (state == NULL || state->native == NULL)
    return SASL_BADPARAM;
  native_length = 0;
  if (state->is_server) {
    return SASL_BADPARAM;
  }
  {
    sasl_interact_t *native_interactions;
    int status;
    status = cpkt_sasl_prepare_pending_interactions(state, interactions,
                                                    &native_interactions);
    if (status != SASL_OK)
      return status;
    status = sasl_client_start(state->native, mechanisms, &native_interactions,
                               output, &native_length, mechanism);
    if (status == SASL_INTERACT) {
      status = cpkt_sasl_store_pending_interactions(state, native_interactions,
                                                    interactions);
    } else {
      cpkt_sasl_clear_pending_interactions(state);
      if (interactions != NULL)
        *interactions = NULL;
    }
    if (output_length != NULL)
      *output_length = (unsigned long)native_length;
    return status;
  }
}

static int cpkt_sasl_step(cpkt_sasl *self, const char *input,
                          unsigned long input_length,
                          cpkt_sasl_interaction **interactions,
                          const char **output, unsigned long *output_length) {
  cpkt_sasl_state *state;
  unsigned native_length;
  state = cpkt_sasl_state_for(self);
  if (output != NULL)
    *output = NULL;
  if (output_length != NULL)
    *output_length = 0;
  if (state == NULL || state->native == NULL || input_length > UINT_MAX)
    return SASL_BADPARAM;
  native_length = 0;
  if (state->is_server) {
    int status;
    status = sasl_server_step(state->native, input, (unsigned)input_length,
                              output, &native_length);
    if (output_length != NULL)
      *output_length = (unsigned long)native_length;
    return status;
  }
  {
    sasl_interact_t *native_interactions;
    int status;
    status = cpkt_sasl_prepare_pending_interactions(state, interactions,
                                                    &native_interactions);
    if (status != SASL_OK)
      return status;
    status = sasl_client_step(state->native, input, (unsigned)input_length,
                              &native_interactions, output, &native_length);
    if (status == SASL_INTERACT) {
      status = cpkt_sasl_store_pending_interactions(state, native_interactions,
                                                    interactions);
    } else {
      cpkt_sasl_clear_pending_interactions(state);
      if (interactions != NULL)
        *interactions = NULL;
    }
    if (output_length != NULL)
      *output_length = (unsigned long)native_length;
    return status;
  }
}

static int cpkt_sasl_server_start(cpkt_sasl *self, const char *mechanism,
                                  const char *input, unsigned long input_length,
                                  const char **output,
                                  unsigned long *output_length) {
  cpkt_sasl_state *state;
  unsigned native_length;
  int status;
  state = cpkt_sasl_state_for(self);
  if (output != NULL)
    *output = NULL;
  if (output_length != NULL)
    *output_length = 0;
  if (state == NULL || state->native == NULL || !state->is_server ||
      mechanism == NULL || input_length > UINT_MAX)
    return SASL_BADPARAM;
  native_length = 0;
  status = sasl_server_start(state->native, mechanism, input,
                             (unsigned)input_length, output, &native_length);
  if (output_length != NULL)
    *output_length = (unsigned long)native_length;
  return status;
}

static int cpkt_sasl_list_mechanisms(cpkt_sasl *self, const char *user,
                                     const char *prefix, const char *separator,
                                     const char *suffix, const char **result,
                                     unsigned long *length, int *count) {
  cpkt_sasl_state *state;
  unsigned native_length;
  int status;
  state = cpkt_sasl_state_for(self);
  if (result != NULL)
    *result = NULL;
  if (length != NULL)
    *length = 0;
  if (count != NULL)
    *count = 0;
  if (state == NULL || state->native == NULL)
    return SASL_BADPARAM;
  native_length = 0;
  status = sasl_listmech(state->native, user, prefix, separator, suffix, result,
                         &native_length, count);
  if (length != NULL)
    *length = (unsigned long)native_length;
  return status;
}

static int cpkt_sasl_encode(cpkt_sasl *self, const char *input,
                            unsigned long input_length, const char **output,
                            unsigned long *output_length) {
  cpkt_sasl_state *state;
  unsigned native_length;
  int status;
  state = cpkt_sasl_state_for(self);
  if (output != NULL)
    *output = NULL;
  if (output_length != NULL)
    *output_length = 0;
  if (state == NULL || state->native == NULL || input_length > UINT_MAX)
    return SASL_BADPARAM;
  native_length = 0;
  status = sasl_encode(state->native, input, (unsigned)input_length, output,
                       &native_length);
  if (output_length != NULL)
    *output_length = (unsigned long)native_length;
  return status;
}

static int cpkt_sasl_decode(cpkt_sasl *self, const char *input,
                            unsigned long input_length, const char **output,
                            unsigned long *output_length) {
  cpkt_sasl_state *state;
  unsigned native_length;
  int status;
  state = cpkt_sasl_state_for(self);
  if (output != NULL)
    *output = NULL;
  if (output_length != NULL)
    *output_length = 0;
  if (state == NULL || state->native == NULL || input_length > UINT_MAX)
    return SASL_BADPARAM;
  native_length = 0;
  status = sasl_decode(state->native, input, (unsigned)input_length, output,
                       &native_length);
  if (output_length != NULL)
    *output_length = (unsigned long)native_length;
  return status;
}

static int cpkt_sasl_set_external_ssf(cpkt_sasl *self, unsigned long value) {
  cpkt_sasl_state *state;
  unsigned native_value;
  state = cpkt_sasl_state_for(self);
  if (state == NULL || state->native == NULL || value > UINT_MAX)
    return SASL_BADPARAM;
  native_value = (unsigned)value;
  return sasl_setprop(state->native, SASL_SSF_EXTERNAL, &native_value);
}

static int cpkt_sasl_set_security_properties(
    cpkt_sasl *self, const cpkt_sasl_security_properties *properties) {
  cpkt_sasl_state *state;
  sasl_security_properties_t native;
  state = cpkt_sasl_state_for(self);
  if (state == NULL || state->native == NULL || properties == NULL ||
      properties->minimum_ssf > UINT_MAX ||
      properties->maximum_ssf > UINT_MAX ||
      properties->maximum_buffer_bytes > UINT_MAX ||
      properties->security_flags > UINT_MAX)
    return SASL_BADPARAM;
  native.min_ssf = (sasl_ssf_t)properties->minimum_ssf;
  native.max_ssf = (sasl_ssf_t)properties->maximum_ssf;
  native.maxbufsize = (unsigned)properties->maximum_buffer_bytes;
  native.security_flags = (unsigned)properties->security_flags;
  native.property_names = (const char **)properties->property_names;
  native.property_values = (const char **)properties->property_values;
  return sasl_setprop(state->native, SASL_SEC_PROPS, &native);
}

static int cpkt_sasl_set_external_authentication(cpkt_sasl *self,
                                                 const char *identity) {
  cpkt_sasl_state *state;
  state = cpkt_sasl_state_for(self);
  if (state == NULL || state->native == NULL)
    return SASL_BADPARAM;
  return sasl_setprop(state->native, SASL_AUTH_EXTERNAL, identity);
}

static const char *cpkt_sasl_error_detail(const cpkt_sasl *self) {
  cpkt_sasl_state *state;
  state = cpkt_sasl_state_for(self);
  return state == NULL || state->native == NULL ? NULL
                                                : sasl_errdetail(state->native);
}

/** Implements the documented public C89 SASL facade operation cpkt_sasl_close.
 */
void cpkt_sasl_close(cpkt_sasl *self) {
  cpkt_sasl_state *state;
  sasl_conn_t *native;
  if (self == NULL)
    return;
  state = cpkt_sasl_state_for(self);
  if (state != NULL) {
    cpkt_sasl_clear_pending_interactions(state);
    native = state->native;
    if (state->native != NULL)
      sasl_dispose(&state->native);
    cpkt_sasl_plugin_utils_forget_connection(native);
    cpkt_sasl_unregister_receiver(state);
    cpkt_sasl_clear_native_secret(state);
    cpkt_sasl_property_discard_borrowed_views(&state->auxiliary_context);
  }
  free(state);
  self->internal = NULL;
  free(self);
}

static int cpkt_sasl_encode_vector(cpkt_sasl *self,
                                   const cpkt_sasl_iov *vectors,
                                   size_t vector_count, const char **output_out,
                                   unsigned long *output_byte_count) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  struct iovec *native;
  unsigned native_length = 0;
  size_t i;
  int status;
  if (output_out != NULL)
    *output_out = NULL;
  if (output_byte_count != NULL)
    *output_byte_count = 0;
  if (state == NULL || state->native == NULL || output_out == NULL ||
      vector_count > UINT_MAX ||
      vector_count > ((size_t)-1) / sizeof(*native) ||
      (vector_count != 0 && vectors == NULL))
    return SASL_BADPARAM;
  native = vector_count == 0
               ? NULL
               : (struct iovec *)calloc(vector_count, sizeof(*native));
  if (vector_count != 0 && native == NULL)
    return SASL_NOMEM;
  for (i = 0; i < vector_count; ++i) {
    native[i].iov_base = (void *)vectors[i].data;
    native[i].iov_len = vectors[i].byte_count;
  }
  status = sasl_encodev(state->native, native, (unsigned)vector_count,
                        output_out, &native_length);
  free(native);
  if (output_byte_count != NULL)
    *output_byte_count = native_length;
  return status;
}

static int cpkt_sasl_check_apop(cpkt_sasl *self, const char *challenge,
                                unsigned long challenge_length,
                                const char *response,
                                unsigned long response_length) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  if (state == NULL || state->native == NULL || challenge_length > UINT_MAX ||
      response_length > UINT_MAX)
    return SASL_BADPARAM;
  return sasl_checkapop(state->native, challenge, (unsigned)challenge_length,
                        response, (unsigned)response_length);
}

static int cpkt_sasl_check_password(cpkt_sasl *self, const char *user,
                                    unsigned long user_length,
                                    const char *password,
                                    unsigned long password_length) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  if (state == NULL || state->native == NULL || user_length > UINT_MAX ||
      password_length > UINT_MAX)
    return SASL_BADPARAM;
  return sasl_checkpass(state->native, user, (unsigned)user_length, password,
                        (unsigned)password_length);
}

static int cpkt_sasl_user_exists(cpkt_sasl *self, const char *service,
                                 const char *realm, const char *user) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  return state == NULL || state->native == NULL
             ? SASL_BADPARAM
             : sasl_user_exists(state->native, service, realm, user);
}

static int
cpkt_sasl_set_password(cpkt_sasl *self, const char *user, const char *password,
                       unsigned long password_length, const char *old_password,
                       unsigned long old_password_length, unsigned long flags) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  if (state == NULL || state->native == NULL || password_length > UINT_MAX ||
      old_password_length > UINT_MAX || flags > UINT_MAX)
    return SASL_BADPARAM;
  return sasl_setpass(state->native, user, password, (unsigned)password_length,
                      old_password, (unsigned)old_password_length,
                      (unsigned)flags);
}

/** Forwards connection or global precomputation to the native idle hooks. */
int cpkt_sasl_idle(cpkt_sasl *self) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  if (self == NULL)
    return sasl_idle(NULL);
  return state == NULL || state->native == NULL ? SASL_BADPARAM
                                                : sasl_idle(state->native);
}

static int cpkt_sasl_auxiliary_request(cpkt_sasl *self,
                                       const char *const *names) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  if (state == NULL || state->native == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_property_discard_borrowed_views(&state->auxiliary_context);
  return sasl_auxprop_request(state->native, (const char **)names);
}

static cpkt_sasl_property_context *
cpkt_sasl_auxiliary_context(cpkt_sasl *self) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  struct propctx *native;
  if (state == NULL || state->native == NULL)
    return NULL;
  native = sasl_auxprop_getctx(state->native);
  if (state->auxiliary_context.native != native) {
    cpkt_sasl_property_discard_borrowed_views(&state->auxiliary_context);
    state->auxiliary_context.native = native;
  }
  state->auxiliary_context.borrowed = 1;
  return state->auxiliary_context.native == NULL ? NULL
                                                 : &state->auxiliary_context;
}

static int cpkt_sasl_auxiliary_store(cpkt_sasl *self,
                                     cpkt_sasl_property_context *context,
                                     const char *user) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  if (state == NULL || state->native == NULL)
    return SASL_BADPARAM;
  return sasl_auxprop_store(state->native,
                            context == NULL ? NULL : context->native, user);
}

static int
cpkt_sasl_set_channel_binding(cpkt_sasl *self,
                              const cpkt_sasl_channel_binding *binding) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  if (state == NULL || state->native == NULL)
    return SASL_BADPARAM;
  if (binding == NULL)
    return sasl_setprop(state->native, SASL_CHANNEL_BINDING, NULL);
  state->channel_binding.name = binding->name;
  state->channel_binding.critical = binding->critical;
  state->channel_binding.len = binding->byte_count;
  state->channel_binding.data = binding->data;
  return sasl_setprop(state->native, SASL_CHANNEL_BINDING,
                      &state->channel_binding);
}

static int cpkt_sasl_set_http_request(cpkt_sasl *self,
                                      const cpkt_sasl_http_request *request) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  if (state == NULL || state->native == NULL)
    return SASL_BADPARAM;
  if (request == NULL)
    return sasl_setprop(state->native, SASL_HTTP_REQUEST, NULL);
  if (request->non_persistent > UINT_MAX)
    return SASL_BADPARAM;
  state->http_request.method = request->method;
  state->http_request.uri = request->uri;
  state->http_request.entity = request->entity;
  state->http_request.elen = request->entity_byte_count;
  state->http_request.non_persist = (unsigned)request->non_persistent;
  return sasl_setprop(state->native, SASL_HTTP_REQUEST, &state->http_request);
}

/** Sets a borrowed GSS credential handle for this SASL receiver. */
static int
cpkt_sasl_set_gss_credentials(cpkt_sasl *self,
                              const cpkt_gss_credential *credentials) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  return state == NULL || state->native == NULL
             ? SASL_BADPARAM
             : sasl_setprop(state->native, SASL_GSS_CREDS, credentials);
}

/** Reads a typed GSS pointer property without exposing a native structure. */
static int cpkt_sasl_get_gss_pointer(const cpkt_sasl *self, int property,
                                     const void **value_out) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  int status;
  if (value_out != NULL)
    *value_out = NULL;
  if (state == NULL || state->native == NULL || value_out == NULL)
    return SASL_BADPARAM;
  status = sasl_getprop(state->native, property, value_out);
  return status;
}

/** Returns the credential previously supplied to this receiver. */
static int
cpkt_sasl_get_gss_credentials(const cpkt_sasl *self,
                              const cpkt_gss_credential **credentials_out) {
  const void *value = NULL;
  int status;
  if (credentials_out == NULL)
    return SASL_BADPARAM;
  *credentials_out = NULL;
  status = cpkt_sasl_get_gss_pointer(self, SASL_GSS_CREDS, &value);
  if (status == SASL_OK)
    *credentials_out = (const cpkt_gss_credential *)value;
  return status;
}

/** Transfers the credential in the provider-owned GSS credential cell. */
static int
cpkt_sasl_get_delegated_credentials(const cpkt_sasl *self,
                                    cpkt_gss_credential **credentials_out) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  const void *value = NULL;
  const void *source = NULL, *mechanism = NULL;
  gss_cred_id_t *cell;
  int status;
  if (credentials_out != NULL)
    *credentials_out = NULL;
  if (state == NULL || credentials_out == NULL)
    return SASL_BADPARAM;
  status = sasl_getprop(state->native, SASL_AUTHSOURCE, &source);
  if (status != SASL_OK)
    return status;
  status = sasl_getprop(state->native, SASL_MECHNAME, &mechanism);
  if (status != SASL_OK)
    return status;
  if (source == NULL || mechanism == NULL ||
      !(((strcmp((const char *)source, "GSSAPI") == 0 ||
          strcmp((const char *)source, "gssapiv2") == 0) &&
         strcmp((const char *)mechanism, "GSSAPI") == 0) ||
        ((strcmp((const char *)source, "GS2") == 0 ||
          strcmp((const char *)source, "gs2") == 0) &&
         strncmp((const char *)mechanism, "GS2-", 4) == 0)))
    return SASL_BADPROT;
  status = cpkt_sasl_get_gss_pointer(self, SASL_DELEGATEDCREDS, &value);
  if (status != SASL_OK)
    return status;
  if (value == NULL)
    return SASL_NOTDONE;
  cell = (gss_cred_id_t *)value;
  if (*cell == GSS_C_NO_CREDENTIAL)
    return SASL_NOTDONE;
  *credentials_out = (cpkt_gss_credential *)*cell;
  *cell = GSS_C_NO_CREDENTIAL;
  return SASL_OK;
}

static int cpkt_sasl_get_delegated_payload(const cpkt_sasl *self,
                                           const void **payload_out) {
  return cpkt_sasl_get_gss_pointer(self, SASL_DELEGATEDCREDS, payload_out);
}

/** Returns the native peer name as a borrowed typed GSS handle. */
static int cpkt_sasl_get_gss_peer_name(const cpkt_sasl *self,
                                       const cpkt_gss_name **name_out) {
  const void *value = NULL;
  int status;
  if (name_out == NULL)
    return SASL_BADPARAM;
  *name_out = NULL;
  status = cpkt_sasl_get_gss_pointer(self, SASL_GSS_PEER_NAME, &value);
  if (status == SASL_OK)
    *name_out = (const cpkt_gss_name *)value;
  return status;
}

/** Returns the native local name as a borrowed typed GSS handle. */
static int cpkt_sasl_get_gss_local_name(const cpkt_sasl *self,
                                        const cpkt_gss_name **name_out) {
  const void *value = NULL;
  int status;
  if (name_out == NULL)
    return SASL_BADPARAM;
  *name_out = NULL;
  status = cpkt_sasl_get_gss_pointer(self, SASL_GSS_LOCAL_NAME, &value);
  if (status == SASL_OK)
    *name_out = (const cpkt_gss_name *)value;
  return status;
}

static int cpkt_sasl_get_text_property(const cpkt_sasl *self, int property,
                                       const char **value_out) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  const void *native = NULL;
  int status;
  if (value_out != NULL)
    *value_out = NULL;
  if (state == NULL || state->native == NULL || value_out == NULL)
    return SASL_BADPARAM;
  switch (property) {
  case SASL_USERNAME:
  case SASL_DEFUSERREALM:
  case SASL_IPLOCALPORT:
  case SASL_IPREMOTEPORT:
  case SASL_PLUGERR:
  case SASL_SERVICE:
  case SASL_SERVERFQDN:
  case SASL_AUTHSOURCE:
  case SASL_MECHNAME:
  case SASL_AUTHUSER:
  case SASL_APPNAME:
  case SASL_AUTH_EXTERNAL:
    break;
  default:
    return SASL_BADPARAM;
  }
  status = sasl_getprop(state->native, property, &native);
  if (status == SASL_OK)
    *value_out = (const char *)native;
  return status;
}

static int cpkt_sasl_get_number_property(const cpkt_sasl *self, int property,
                                         unsigned long *value_out) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  const void *native = NULL;
  int status;
  if (value_out != NULL)
    *value_out = 0;
  if (state == NULL || state->native == NULL || value_out == NULL ||
      (property != SASL_SSF && property != SASL_MAXOUTBUF &&
       property != SASL_SSF_EXTERNAL))
    return SASL_BADPARAM;
  status = sasl_getprop(state->native, property, &native);
  if (status == SASL_OK && native != NULL)
    *value_out = *(const unsigned *)native;
  return status;
}

static void cpkt_sasl_set_error(cpkt_sasl *self, unsigned long flags,
                                const char *message) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  if (state != NULL && state->native != NULL && flags <= UINT_MAX &&
      message != NULL)
    sasl_seterror(state->native, (unsigned)flags, "%s", message);
}

static int cpkt_sasl_make_challenge(cpkt_sasl *self, char *buffer,
                                    unsigned long capacity,
                                    unsigned long host_flag) {
  cpkt_sasl_state *state = cpkt_sasl_state_for(self);
  if (state == NULL || state->native == NULL || capacity > UINT_MAX ||
      host_flag > UINT_MAX)
    return SASL_BADPARAM;
  return sasl_mkchal(state->native, buffer, (unsigned)capacity,
                     (unsigned)host_flag);
}

/** Implements the documented public C89 SASL facade operation
 * cpkt_sasl_version. */
void cpkt_sasl_version(const char **implementation, const char **version,
                       int *major, int *minor, int *step, int *patch) {
  sasl_version_info(implementation, version, major, minor, step, patch);
}

/** Returns the native legacy version number with borrowed vendor text. */
void cpkt_sasl_legacy_version(const char **implementation, int *version) {
  sasl_version(implementation, version);
}

/** Implements the documented public C89 SASL facade operation
 * cpkt_sasl_error_string. */
const char *cpkt_sasl_error_string(int status, const char *languages,
                                   const char **language) {
  return sasl_errstring(status, languages, language);
}

/** Implements the documented public C89 SASL facade operation
 * cpkt_sasl_set_path. */
int cpkt_sasl_set_path(int type, const char *path) {
  return sasl_set_path(type, (char *)path);
}

/** Implements the documented public C89 SASL facade operation
 * cpkt_sasl_client_initialize. */
int cpkt_sasl_client_initialize(const cpkt_sasl_callbacks *callbacks) {
  int status;
  if (cpkt_sasl_client_callbacks.references == ULONG_MAX)
    return SASL_FAIL;
  if (cpkt_sasl_client_callbacks.references == 0)
    cpkt_sasl_callbacks_build(
        cpkt_sasl_client_callbacks.native,
        &cpkt_sasl_client_callbacks.callback_owner.callbacks, callbacks,
        &cpkt_sasl_client_callbacks.callback_owner);
  status = sasl_client_init(callbacks == NULL ||
                                    cpkt_sasl_client_callbacks.references != 0
                                ? NULL
                                : cpkt_sasl_client_callbacks.native);
  if (status == SASL_OK) {
    if (cpkt_sasl_client_callbacks.references == 0)
      cpkt_sasl_plugin_utils_update_global_context(
          0, cpkt_sasl_global_option_application_context(0), 1);
    ++cpkt_sasl_client_callbacks.references;
  }
  return status;
}

/** Implements the documented public C89 SASL facade operation
 * cpkt_sasl_server_initialize. */
int cpkt_sasl_server_initialize(const cpkt_sasl_callbacks *callbacks,
                                const char *application_name) {
  int status;
  if (cpkt_sasl_server_callbacks.references == ULONG_MAX)
    return SASL_FAIL;
  if (cpkt_sasl_server_callbacks.references == 0)
    cpkt_sasl_callbacks_build(
        cpkt_sasl_server_callbacks.native,
        &cpkt_sasl_server_callbacks.callback_owner.callbacks, callbacks,
        &cpkt_sasl_server_callbacks.callback_owner);
  status = sasl_server_init(callbacks == NULL ||
                                    cpkt_sasl_server_callbacks.references != 0
                                ? NULL
                                : cpkt_sasl_server_callbacks.native,
                            application_name);
  if (status == SASL_OK) {
    if (cpkt_sasl_server_callbacks.references == 0)
      cpkt_sasl_plugin_utils_update_global_context(
          1, cpkt_sasl_global_option_application_context(1), 1);
    ++cpkt_sasl_server_callbacks.references;
  }
  return status;
}

/** Implements the documented public C89 SASL facade operation
 * cpkt_sasl_client_finish. */
int cpkt_sasl_client_finish(void) {
  int status;
  status = sasl_client_done();
  if ((status == SASL_OK || status == SASL_CONTINUE) &&
      cpkt_sasl_client_callbacks.references != 0)
    --cpkt_sasl_client_callbacks.references;
  if (status == SASL_OK && cpkt_sasl_client_callbacks.references == 0)
    cpkt_sasl_client_plugins_cleanup();
  if (status == SASL_OK && cpkt_sasl_client_callbacks.references == 0) {
    /* Shared Cyrus utilities can retain this role's native callback record.
     * Retire the application callbacks at their documented finish boundary. */
    memset(&cpkt_sasl_client_callbacks.callback_owner.callbacks, 0,
           sizeof(cpkt_sasl_client_callbacks.callback_owner.callbacks));
    cpkt_sasl_plugin_utils_update_global_context(0, NULL, 0);
  }
  if (status == SASL_OK && cpkt_sasl_client_callbacks.references == 0 &&
      cpkt_sasl_server_callbacks.references == 0)
    cpkt_sasl_canonicalizers_cleanup();
  if (status == SASL_OK && cpkt_sasl_client_callbacks.references == 0 &&
      cpkt_sasl_server_callbacks.references == 0)
    cpkt_sasl_plugin_utils_cleanup();
  return status;
}
/** Implements the documented public C89 SASL facade operation
 * cpkt_sasl_server_finish. */
int cpkt_sasl_server_finish(void) {
  int status;
  status = sasl_server_done();
  if ((status == SASL_OK || status == SASL_CONTINUE) &&
      cpkt_sasl_server_callbacks.references != 0)
    --cpkt_sasl_server_callbacks.references;
  if (status == SASL_OK && cpkt_sasl_server_callbacks.references == 0)
    cpkt_sasl_auxiliary_plugins_cleanup();
  if (status == SASL_OK && cpkt_sasl_server_callbacks.references == 0) {
    /* Shared Cyrus utilities can retain this role's native callback record.
     * Retire the application callbacks at their documented finish boundary. */
    memset(&cpkt_sasl_server_callbacks.callback_owner.callbacks, 0,
           sizeof(cpkt_sasl_server_callbacks.callback_owner.callbacks));
    cpkt_sasl_plugin_utils_update_global_context(1, NULL, 0);
  }
  if (status == SASL_OK && cpkt_sasl_server_callbacks.references == 0)
    cpkt_sasl_server_plugins_cleanup();
  if (status == SASL_OK && cpkt_sasl_client_callbacks.references == 0 &&
      cpkt_sasl_server_callbacks.references == 0)
    cpkt_sasl_canonicalizers_cleanup();
  if (status == SASL_OK && cpkt_sasl_client_callbacks.references == 0 &&
      cpkt_sasl_server_callbacks.references == 0)
    cpkt_sasl_plugin_utils_cleanup();
  return status;
}

/** C89 facade contract for cpkt_sasl_finish_all; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_finish_all(void) {
  sasl_done();
  if (cpkt_sasl_client_callbacks.references != 0)
    --cpkt_sasl_client_callbacks.references;
  if (cpkt_sasl_server_callbacks.references != 0)
    --cpkt_sasl_server_callbacks.references;
  if (cpkt_sasl_client_callbacks.references == 0) {
    cpkt_sasl_plugin_utils_update_global_context(0, NULL, 0);
    cpkt_sasl_client_plugins_cleanup();
    memset(&cpkt_sasl_client_callbacks, 0, sizeof(cpkt_sasl_client_callbacks));
  }
  if (cpkt_sasl_server_callbacks.references == 0) {
    cpkt_sasl_plugin_utils_update_global_context(1, NULL, 0);
    cpkt_sasl_auxiliary_plugins_cleanup();
    cpkt_sasl_server_plugins_cleanup();
    memset(&cpkt_sasl_server_callbacks, 0, sizeof(cpkt_sasl_server_callbacks));
  }
  if (cpkt_sasl_client_callbacks.references == 0 &&
      cpkt_sasl_server_callbacks.references == 0) {
    cpkt_sasl_canonicalizers_cleanup();
    cpkt_sasl_plugin_utils_cleanup();
  }
}

/** C89 facade contract for cpkt_sasl_global_mechanisms; see the public header
 * for ownership and callback lifetime. */
const char *const *cpkt_sasl_global_mechanisms(void) {
  return (const char *const *)sasl_global_listmech();
}

static cpkt_sasl *cpkt_sasl_new(int is_server, const char *service,
                                const char *server_name, const char *realm,
                                const char *local_endpoint,
                                const char *remote_endpoint,
                                const cpkt_sasl_callbacks *callbacks,
                                unsigned long flags, int *status_out) {
  cpkt_sasl *self;
  cpkt_sasl_state *state;
  int status;
  if (status_out != NULL)
    *status_out = SASL_BADPARAM;
  if (service == NULL || flags > UINT_MAX)
    return NULL;
  self = (cpkt_sasl *)calloc(1, sizeof(*self));
  state = (cpkt_sasl_state *)calloc(1, sizeof(*state));
  if (self == NULL || state == NULL) {
    free(self);
    free(state);
    if (status_out != NULL)
      *status_out = SASL_NOMEM;
    return NULL;
  }
  state->is_server = is_server;
  self->internal = state;
  state->callback_owner.public_receiver = self;
  cpkt_sasl_callbacks_build(state->native_callbacks,
                            &state->callback_owner.callbacks, callbacks,
                            &state->callback_owner);
  if (is_server) {
    status = sasl_server_new(service, server_name, realm, local_endpoint,
                             remote_endpoint,
                             callbacks == NULL ? NULL : state->native_callbacks,
                             (unsigned)flags, &state->native);
  } else {
    status =
        sasl_client_new(service, server_name, local_endpoint, remote_endpoint,
                        callbacks == NULL ? NULL : state->native_callbacks,
                        (unsigned)flags, &state->native);
  }
  if (status != SASL_OK) {
    cpkt_sasl_clear_native_secret(state);
    free(state);
    free(self);
    if (status_out != NULL)
      *status_out = status;
    return NULL;
  }
  cpkt_sasl_register_receiver(state);
  self->start = cpkt_sasl_start;
  self->step = cpkt_sasl_step;
  self->server_start = cpkt_sasl_server_start;
  self->list_mechanisms = cpkt_sasl_list_mechanisms;
  self->encode = cpkt_sasl_encode;
  self->decode = cpkt_sasl_decode;
  self->set_external_ssf = cpkt_sasl_set_external_ssf;
  self->set_security_properties = cpkt_sasl_set_security_properties;
  self->set_external_authentication = cpkt_sasl_set_external_authentication;
  self->error_detail = cpkt_sasl_error_detail;
  self->close = cpkt_sasl_close;
  self->internal = state;
  self->encode_vector = cpkt_sasl_encode_vector;
  self->check_apop = cpkt_sasl_check_apop;
  self->check_password = cpkt_sasl_check_password;
  self->user_exists = cpkt_sasl_user_exists;
  self->set_password = cpkt_sasl_set_password;
  self->idle = cpkt_sasl_idle;
  self->auxiliary_request = cpkt_sasl_auxiliary_request;
  self->auxiliary_context = cpkt_sasl_auxiliary_context;
  self->auxiliary_store = cpkt_sasl_auxiliary_store;
  self->set_channel_binding = cpkt_sasl_set_channel_binding;
  self->set_http_request = cpkt_sasl_set_http_request;
  self->set_gss_credentials = cpkt_sasl_set_gss_credentials;
  self->get_gss_credentials = cpkt_sasl_get_gss_credentials;
  self->get_delegated_credentials = cpkt_sasl_get_delegated_credentials;
  self->get_delegated_payload = cpkt_sasl_get_delegated_payload;
  self->get_gss_peer_name = cpkt_sasl_get_gss_peer_name;
  self->get_gss_local_name = cpkt_sasl_get_gss_local_name;
  self->get_text_property = cpkt_sasl_get_text_property;
  self->get_number_property = cpkt_sasl_get_number_property;
  self->set_text_property = cpkt_sasl_connection_set_text;
  self->get_security_properties = cpkt_sasl_connection_get_security;
  self->get_http_request = cpkt_sasl_connection_get_http;
  self->get_callback_record = cpkt_sasl_connection_get_callbacks;
  self->get_option_context = cpkt_sasl_connection_get_option_context;
  self->set_error = cpkt_sasl_set_error;
  self->make_challenge = cpkt_sasl_make_challenge;
  if (status_out != NULL)
    *status_out = SASL_OK;
  return self;
}

/** Implements the documented public C89 SASL facade operation
 * cpkt_sasl_client_new. */
cpkt_sasl *cpkt_sasl_client_new(const char *service, const char *server_name,
                                const char *local_endpoint,
                                const char *remote_endpoint,
                                const cpkt_sasl_callbacks *callbacks,
                                unsigned long flags, int *status_out) {
  return cpkt_sasl_new(0, service, server_name, NULL, local_endpoint,
                       remote_endpoint, callbacks, flags, status_out);
}

/** Implements the documented public C89 SASL facade operation
 * cpkt_sasl_server_new. */
cpkt_sasl *cpkt_sasl_server_new(const char *service, const char *server_name,
                                const char *user_realm,
                                const char *local_endpoint,
                                const char *remote_endpoint,
                                const cpkt_sasl_callbacks *callbacks,
                                unsigned long flags, int *status_out) {
  return cpkt_sasl_new(1, service, server_name, user_realm, local_endpoint,
                       remote_endpoint, callbacks, flags, status_out);
}
