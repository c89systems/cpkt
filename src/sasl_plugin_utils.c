#include <cpkt/sasl_plugin.h>

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <sasl/sasl.h>
#include <sasl/saslplug.h>

#include "sasl_private.h"

typedef char
    cpkt_sasl_md5_word_is_32_bits[(sizeof(unsigned int) * CHAR_BIT == 32) ? 1
                                                                          : -1];
typedef char
    cpkt_sasl_native_md5_word_is_32_bits[(sizeof(UINT4) * CHAR_BIT == 32) ? 1
                                                                          : -1];

typedef union cpkt_sasl_selected_native_proc {
  sasl_callback_ft generic;
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
} cpkt_sasl_selected_native_proc;

typedef struct cpkt_sasl_selected_callback {
  cpkt_sasl_callbacks callbacks;
  cpkt_sasl_selected_native_proc native;
  void *native_context;
  sasl_conn_t *connection;
  cpkt_sasl_secret secret;
  unsigned long id;
} cpkt_sasl_selected_callback;

typedef struct cpkt_sasl_utils_entry {
  const sasl_utils_t *native;
  sasl_conn_t *connection;
  void *global_option_context;
  cpkt_sasl_plugin_utils public_utils;
  cpkt_sasl_selected_callback selected[17];
  struct cpkt_sasl_utils_entry *next;
} cpkt_sasl_utils_entry;

static pthread_mutex_t cpkt_sasl_utils_lock = PTHREAD_MUTEX_INITIALIZER;
static cpkt_sasl_utils_entry *cpkt_sasl_utils_entries;

cpkt_sasl_plugin_utils *
cpkt_sasl_plugin_utils_for_native(const sasl_utils_t *native) {
  cpkt_sasl_utils_entry *entry;
  if (native == NULL)
    return NULL;
  (void)pthread_mutex_lock(&cpkt_sasl_utils_lock);
  for (entry = cpkt_sasl_utils_entries; entry != NULL; entry = entry->next) {
    if (entry->native == native)
      break;
  }
  if (entry == NULL) {
    entry = (cpkt_sasl_utils_entry *)calloc(1, sizeof(*entry));
    if (entry != NULL) {
      entry->native = native;
      entry->connection = native->conn;
      cpkt_sasl_plugin_utils_initialize(&entry->public_utils, native);
      entry->next = cpkt_sasl_utils_entries;
      cpkt_sasl_utils_entries = entry;
    }
  } else {
    entry->connection = native->conn;
    cpkt_sasl_plugin_utils_initialize(&entry->public_utils, native);
  }
  if (entry != NULL && native->conn == NULL)
    entry->public_utils.option_context = entry->global_option_context;
  (void)pthread_mutex_unlock(&cpkt_sasl_utils_lock);
  return entry == NULL ? NULL : &entry->public_utils;
}

/** Retains the application option context on the persistent native utility
 * table before a client or server plugin initializer receives it. */
void cpkt_sasl_plugin_utils_set_global_option_context(
    const sasl_utils_t *native, void *context) {
  cpkt_sasl_utils_entry *entry;
  (void)pthread_mutex_lock(&cpkt_sasl_utils_lock);
  for (entry = cpkt_sasl_utils_entries; entry != NULL; entry = entry->next) {
    if (entry->native == native && entry->connection == NULL) {
      entry->global_option_context = context;
      entry->public_utils.option_context = context;
      break;
    }
  }
  (void)pthread_mutex_unlock(&cpkt_sasl_utils_lock);
}

/** Drops per-connection utility records after native mechanism disposal and
 * before the receiver's native address can be reused. */
void cpkt_sasl_plugin_utils_forget_connection(sasl_conn_t *native) {
  cpkt_sasl_utils_entry **link;
  cpkt_sasl_utils_entry *entry;
  (void)pthread_mutex_lock(&cpkt_sasl_utils_lock);
  link = &cpkt_sasl_utils_entries;
  while (*link != NULL) {
    entry = *link;
    if (entry->connection == native && native != NULL) {
      *link = entry->next;
      free(entry);
    } else {
      link = &entry->next;
    }
  }
  (void)pthread_mutex_unlock(&cpkt_sasl_utils_lock);
}

/** Releases remaining global utility records after the final role shutdown;
 * native mech_free callbacks have already completed. */
void cpkt_sasl_plugin_utils_cleanup(void) {
  cpkt_sasl_utils_entry *entry, *next;
  (void)pthread_mutex_lock(&cpkt_sasl_utils_lock);
  entry = cpkt_sasl_utils_entries;
  cpkt_sasl_utils_entries = NULL;
  (void)pthread_mutex_unlock(&cpkt_sasl_utils_lock);
  while (entry != NULL) {
    next = entry->next;
    free(entry);
    entry = next;
  }
}

static const sasl_utils_t *
cpkt_sasl_native_utils(const cpkt_sasl_plugin_utils *self) {
  return self == NULL ? NULL : (const sasl_utils_t *)self->internal;
}

static void cpkt_sasl_md5_to_native(MD5_CTX *native,
                                    const cpkt_sasl_md5_context *public_ctx) {
  size_t i;
  for (i = 0; i < 4; ++i)
    native->state[i] = public_ctx->state[i];
  for (i = 0; i < 2; ++i)
    native->count[i] = public_ctx->count[i];
  memcpy(native->buffer, public_ctx->buffer, sizeof(native->buffer));
}

static void cpkt_sasl_md5_from_native(cpkt_sasl_md5_context *public_ctx,
                                      const MD5_CTX *native) {
  size_t i;
  for (i = 0; i < 4; ++i)
    public_ctx->state[i] = native->state[i];
  for (i = 0; i < 2; ++i)
    public_ctx->count[i] = native->count[i];
  memcpy(public_ctx->buffer, native->buffer, sizeof(public_ctx->buffer));
}

static void
cpkt_sasl_hmac_to_native(HMAC_MD5_CTX *native,
                         const cpkt_sasl_hmac_md5_context *public_ctx) {
  cpkt_sasl_md5_to_native(&native->ictx, &public_ctx->inner);
  cpkt_sasl_md5_to_native(&native->octx, &public_ctx->outer);
}

static void cpkt_sasl_hmac_from_native(cpkt_sasl_hmac_md5_context *public_ctx,
                                       const HMAC_MD5_CTX *native) {
  cpkt_sasl_md5_from_native(&public_ctx->inner, &native->ictx);
  cpkt_sasl_md5_from_native(&public_ctx->outer, &native->octx);
}

static int cpkt_sasl_plugin_option(const cpkt_sasl_plugin_utils *self,
                                   const char *plugin, const char *name,
                                   const char **result,
                                   unsigned long *result_length) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  unsigned length = 0;
  int status;
  if (result_length != NULL)
    *result_length = 0;
  if (native == NULL || native->getopt == NULL)
    return SASL_BADPARAM;
  status =
      native->getopt(native->getopt_context, plugin, name, result, &length);
  if (result_length != NULL)
    *result_length = length;
  return status;
}

static void *cpkt_sasl_plugin_allocate(const cpkt_sasl_plugin_utils *self,
                                       size_t size) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  return native == NULL || native->malloc == NULL ? NULL : native->malloc(size);
}
static void *
cpkt_sasl_plugin_allocate_zeroed(const cpkt_sasl_plugin_utils *self,
                                 size_t count, size_t size) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  return native == NULL || native->calloc == NULL ? NULL
                                                  : native->calloc(count, size);
}
static void *cpkt_sasl_plugin_resize(const cpkt_sasl_plugin_utils *self,
                                     void *value, size_t size) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  return native == NULL || native->realloc == NULL
             ? NULL
             : native->realloc(value, size);
}
static void cpkt_sasl_plugin_release(const cpkt_sasl_plugin_utils *self,
                                     void *value) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native != NULL && native->free != NULL)
    native->free(value);
}
static void *cpkt_sasl_plugin_mutex_new(const cpkt_sasl_plugin_utils *self) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  return native == NULL || native->mutex_alloc == NULL ? NULL
                                                       : native->mutex_alloc();
}
static int cpkt_sasl_plugin_mutex_lock(const cpkt_sasl_plugin_utils *self,
                                       void *mutex) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  return native == NULL || native->mutex_lock == NULL
             ? SASL_BADPARAM
             : native->mutex_lock(mutex);
}
static int cpkt_sasl_plugin_mutex_unlock(const cpkt_sasl_plugin_utils *self,
                                         void *mutex) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  return native == NULL || native->mutex_unlock == NULL
             ? SASL_BADPARAM
             : native->mutex_unlock(mutex);
}
static void cpkt_sasl_plugin_mutex_free(const cpkt_sasl_plugin_utils *self,
                                        void *mutex) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native != NULL && native->mutex_free != NULL)
    native->mutex_free(mutex);
}

static void cpkt_sasl_plugin_md5_init(const cpkt_sasl_plugin_utils *self,
                                      cpkt_sasl_md5_context *context) {
  const sasl_utils_t *utils = cpkt_sasl_native_utils(self);
  MD5_CTX native;
  if (utils == NULL || utils->MD5Init == NULL || context == NULL)
    return;
  memset(&native, 0, sizeof(native));
  utils->MD5Init(&native);
  cpkt_sasl_md5_from_native(context, &native);
}
static int cpkt_sasl_plugin_md5_update(const cpkt_sasl_plugin_utils *self,
                                       cpkt_sasl_md5_context *context,
                                       const unsigned char *bytes,
                                       unsigned long length) {
  const sasl_utils_t *utils = cpkt_sasl_native_utils(self);
  MD5_CTX native;
  if (utils == NULL || utils->MD5Update == NULL || context == NULL ||
      (bytes == NULL && length != 0) || length > UINT_MAX)
    return SASL_BADPARAM;
  cpkt_sasl_md5_to_native(&native, context);
  utils->MD5Update(&native, bytes, (unsigned)length);
  cpkt_sasl_md5_from_native(context, &native);
  return SASL_OK;
}
static void cpkt_sasl_plugin_md5_final(const cpkt_sasl_plugin_utils *self,
                                       unsigned char digest[16],
                                       cpkt_sasl_md5_context *context) {
  const sasl_utils_t *utils = cpkt_sasl_native_utils(self);
  MD5_CTX native;
  if (utils == NULL || utils->MD5Final == NULL || context == NULL ||
      digest == NULL)
    return;
  cpkt_sasl_md5_to_native(&native, context);
  utils->MD5Final(digest, &native);
  cpkt_sasl_md5_from_native(context, &native);
}

static void cpkt_sasl_plugin_hmac_md5(const cpkt_sasl_plugin_utils *self,
                                      const unsigned char *text,
                                      int text_length, const unsigned char *key,
                                      int key_length,
                                      unsigned char digest[16]) {
  const sasl_utils_t *utils = cpkt_sasl_native_utils(self);
  if (utils != NULL && utils->hmac_md5 != NULL && digest != NULL &&
      text_length >= 0 && key_length >= 0 &&
      (text != NULL || text_length == 0) && (key != NULL || key_length == 0))
    utils->hmac_md5(text, text_length, key, key_length, digest);
}
static void cpkt_sasl_plugin_hmac_init(const cpkt_sasl_plugin_utils *self,
                                       cpkt_sasl_hmac_md5_context *context,
                                       const unsigned char *key,
                                       int key_length) {
  const sasl_utils_t *utils = cpkt_sasl_native_utils(self);
  HMAC_MD5_CTX native;
  if (utils == NULL || utils->hmac_md5_init == NULL || context == NULL ||
      key_length < 0 || (key == NULL && key_length != 0))
    return;
  memset(&native, 0, sizeof(native));
  utils->hmac_md5_init(&native, key, key_length);
  cpkt_sasl_hmac_from_native(context, &native);
}
static int cpkt_sasl_plugin_hmac_update(const cpkt_sasl_plugin_utils *self,
                                        cpkt_sasl_hmac_md5_context *context,
                                        const unsigned char *bytes,
                                        unsigned long length) {
  const sasl_utils_t *utils = cpkt_sasl_native_utils(self);
  HMAC_MD5_CTX native;
  if (utils == NULL || utils->MD5Update == NULL || context == NULL ||
      (bytes == NULL && length != 0) || length > UINT_MAX)
    return SASL_BADPARAM;
  cpkt_sasl_hmac_to_native(&native, context);
  utils->MD5Update(&native.ictx, bytes, (unsigned)length);
  cpkt_sasl_hmac_from_native(context, &native);
  return SASL_OK;
}
static void cpkt_sasl_plugin_hmac_final(const cpkt_sasl_plugin_utils *self,
                                        unsigned char digest[16],
                                        cpkt_sasl_hmac_md5_context *context) {
  const sasl_utils_t *utils = cpkt_sasl_native_utils(self);
  HMAC_MD5_CTX native;
  if (utils == NULL || utils->hmac_md5_final == NULL || context == NULL ||
      digest == NULL)
    return;
  cpkt_sasl_hmac_to_native(&native, context);
  utils->hmac_md5_final(digest, &native);
  cpkt_sasl_hmac_from_native(context, &native);
}
static void
cpkt_sasl_plugin_hmac_precalculate(const cpkt_sasl_plugin_utils *self,
                                   cpkt_sasl_hmac_md5_state *state,
                                   const unsigned char *key, int key_length) {
  const sasl_utils_t *utils = cpkt_sasl_native_utils(self);
  HMAC_MD5_STATE native;
  size_t i;
  if (utils == NULL || utils->hmac_md5_precalc == NULL || state == NULL ||
      key_length < 0 || (key == NULL && key_length != 0))
    return;
  utils->hmac_md5_precalc(&native, key, key_length);
  for (i = 0; i < 4; ++i) {
    state->inner[i] = native.istate[i];
    state->outer[i] = native.ostate[i];
  }
}
static void
cpkt_sasl_plugin_hmac_import(const cpkt_sasl_plugin_utils *self,
                             cpkt_sasl_hmac_md5_context *context,
                             const cpkt_sasl_hmac_md5_state *state) {
  const sasl_utils_t *utils = cpkt_sasl_native_utils(self);
  HMAC_MD5_CTX native_context;
  HMAC_MD5_STATE native_state;
  size_t i;
  if (utils == NULL || utils->hmac_md5_import == NULL || context == NULL ||
      state == NULL)
    return;
  memset(&native_context, 0, sizeof(native_context));
  for (i = 0; i < 4; ++i) {
    native_state.istate[i] = state->inner[i];
    native_state.ostate[i] = state->outer[i];
  }
  utils->hmac_md5_import(&native_context, &native_state);
  cpkt_sasl_hmac_from_native(context, &native_context);
}

static int cpkt_sasl_plugin_make_challenge(const cpkt_sasl_plugin_utils *self,
                                           char *output, unsigned long capacity,
                                           unsigned long host_flag) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native == NULL || native->mkchal == NULL || capacity > UINT_MAX ||
      host_flag > UINT_MAX)
    return SASL_BADPARAM;
  return native->mkchal(native->conn, output, (unsigned)capacity,
                        (unsigned)host_flag);
}
static int cpkt_sasl_plugin_utf8_verify(const cpkt_sasl_plugin_utils *self,
                                        const char *text,
                                        unsigned long length) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native == NULL || native->utf8verify == NULL || length > UINT_MAX)
    return SASL_BADPARAM;
  return native->utf8verify(text, (unsigned)length);
}
static int cpkt_sasl_plugin_random_fill(const cpkt_sasl_plugin_utils *self,
                                        char *output, unsigned long length) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native == NULL || native->rand == NULL || length > UINT_MAX)
    return SASL_BADPARAM;
  native->rand(native->rpool, output, (unsigned)length);
  return SASL_OK;
}
static int cpkt_sasl_plugin_random_churn(const cpkt_sasl_plugin_utils *self,
                                         const char *bytes,
                                         unsigned long length) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native == NULL || native->churn == NULL || length > UINT_MAX)
    return SASL_BADPARAM;
  native->churn(native->rpool, bytes, (unsigned)length);
  return SASL_OK;
}
static int cpkt_sasl_plugin_check_password(const cpkt_sasl_plugin_utils *self,
                                           const char *user,
                                           unsigned long user_length,
                                           const char *password,
                                           unsigned long password_length) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native == NULL || native->checkpass == NULL || user_length > UINT_MAX ||
      password_length > UINT_MAX)
    return SASL_BADPARAM;
  return native->checkpass(native->conn, user, (unsigned)user_length, password,
                           (unsigned)password_length);
}

static int cpkt_sasl_plugin_base64(const cpkt_sasl_plugin_utils *self,
                                   int encode, const char *input,
                                   unsigned long input_length, char *output,
                                   unsigned long capacity,
                                   unsigned long *output_length) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  unsigned native_length = 0;
  int status;
  if (output_length != NULL)
    *output_length = 0;
  if (native == NULL || input_length > UINT_MAX || capacity > UINT_MAX)
    return SASL_BADPARAM;
  if (encode) {
    if (native->encode64 == NULL)
      return SASL_BADPARAM;
    status = native->encode64(input, (unsigned)input_length, output,
                              (unsigned)capacity, &native_length);
  } else {
    if (native->decode64 == NULL)
      return SASL_BADPARAM;
    status = native->decode64(input, (unsigned)input_length, output,
                              (unsigned)capacity, &native_length);
  }
  if (output_length != NULL)
    *output_length = native_length;
  return status;
}
static int cpkt_sasl_plugin_base64_decode(const cpkt_sasl_plugin_utils *self,
                                          const char *input,
                                          unsigned long input_length,
                                          char *output, unsigned long capacity,
                                          unsigned long *output_length) {
  return cpkt_sasl_plugin_base64(self, 0, input, input_length, output, capacity,
                                 output_length);
}
static int cpkt_sasl_plugin_base64_encode(const cpkt_sasl_plugin_utils *self,
                                          const char *input,
                                          unsigned long input_length,
                                          char *output, unsigned long capacity,
                                          unsigned long *output_length) {
  return cpkt_sasl_plugin_base64(self, 1, input, input_length, output, capacity,
                                 output_length);
}

static int
cpkt_sasl_plugin_get_text_property(const cpkt_sasl_plugin_utils *self,
                                   int property, const char **value_out) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  const void *value = NULL;
  int status;
  if (value_out != NULL)
    *value_out = NULL;
  if (native == NULL || native->getprop == NULL || value_out == NULL)
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
  status = native->getprop(native->conn, property, &value);
  if (status == SASL_OK)
    *value_out = (const char *)value;
  return status;
}
static int
cpkt_sasl_plugin_get_number_property(const cpkt_sasl_plugin_utils *self,
                                     int property, unsigned long *value_out) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  const void *value = NULL;
  int status;
  if (value_out != NULL)
    *value_out = 0;
  if (native == NULL || native->getprop == NULL || value_out == NULL ||
      (property != SASL_SSF && property != SASL_MAXOUTBUF &&
       property != SASL_SSF_EXTERNAL))
    return SASL_BADPARAM;
  status = native->getprop(native->conn, property, &value);
  if (status == SASL_OK && value != NULL)
    *value_out = *(const unsigned *)value;
  return status;
}
static int cpkt_sasl_plugin_set_external_ssf(const cpkt_sasl_plugin_utils *self,
                                             unsigned long value) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  unsigned native_value;
  if (native == NULL || native->setprop == NULL || value > UINT_MAX)
    return SASL_BADPARAM;
  native_value = (unsigned)value;
  return native->setprop(native->conn, SASL_SSF_EXTERNAL, &native_value);
}
static int
cpkt_sasl_plugin_set_external_authentication(const cpkt_sasl_plugin_utils *self,
                                             const char *identity) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  return native == NULL || native->setprop == NULL
             ? SASL_BADPARAM
             : native->setprop(native->conn, SASL_AUTH_EXTERNAL, identity);
}
static int
cpkt_sasl_plugin_set_text_property(const cpkt_sasl_plugin_utils *self,
                                   int property, const char *value) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native == NULL || native->setprop == NULL)
    return SASL_BADPARAM;
  switch (property) {
  case SASL_AUTH_EXTERNAL:
  case SASL_DEFUSERREALM:
  case SASL_APPNAME:
  case SASL_IPLOCALPORT:
  case SASL_IPREMOTEPORT:
    return native->setprop(native->conn, property, value);
  default:
    return SASL_BADPARAM;
  }
}
static int cpkt_sasl_selected_option(void *context, const char *plugin,
                                     const char *name, const char **result,
                                     unsigned long *length_out) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  unsigned length = 0;
  int status = slot->native.option(slot->native_context, plugin, name, result,
                                   length_out == NULL ? NULL : &length);
  if (length_out != NULL)
    *length_out = length;
  return status;
}
static int cpkt_sasl_selected_log(void *context, int level,
                                  const char *message) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  return slot->native.log(slot->native_context, level, message);
}
static int cpkt_sasl_selected_path(void *context, const char **path) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  return slot->native.path(slot->native_context, path);
}
static int cpkt_sasl_selected_verify(void *context, const char *path,
                                     int type) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  return slot->native.verify(slot->native_context, path,
                             (sasl_verify_type_t)type);
}
static int cpkt_sasl_selected_confpath(void *context, char **path) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  return slot->native.configuration_path(slot->native_context, path);
}
static int cpkt_sasl_selected_simple(void *context, int id, const char **result,
                                     unsigned long *length_out) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  unsigned length = 0;
  int status = slot->native.simple(slot->native_context, id, result,
                                   length_out == NULL ? NULL : &length);
  if (length_out != NULL)
    *length_out = length;
  return status;
}
static int cpkt_sasl_selected_secret(cpkt_sasl *receiver, void *context, int id,
                                     const cpkt_sasl_secret **secret_out) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  sasl_secret_t *native_secret = NULL;
  int status;
  (void)receiver;
  if (secret_out == NULL || slot->connection == NULL)
    return SASL_BADPARAM;
  *secret_out = NULL;
  status = slot->native.secret(slot->connection, slot->native_context, id,
                               &native_secret);
  if (status == SASL_OK && native_secret != NULL) {
    slot->secret.data = native_secret->data;
    slot->secret.byte_count = native_secret->len;
    *secret_out = &slot->secret;
  }
  return status;
}
static int
cpkt_sasl_selected_challenge(void *context, int id, const char *challenge,
                             const char *prompt, const char *default_result,
                             const char **result, unsigned long *length_out) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  unsigned length = 0;
  int status = slot->native.challenge(slot->native_context, id, challenge,
                                      prompt, default_result, result,
                                      length_out == NULL ? NULL : &length);
  if (length_out != NULL)
    *length_out = length;
  return status;
}
static int cpkt_sasl_selected_realm(void *context, int id,
                                    const char *const *realms,
                                    const char **result) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  return slot->native.realm(slot->native_context, id, (const char **)realms,
                            result);
}
static int cpkt_sasl_selected_authorize(cpkt_sasl *receiver, void *context,
                                        const char *requested,
                                        unsigned long requested_length,
                                        const char *authentication,
                                        unsigned long authentication_length,
                                        const char *realm,
                                        unsigned long realm_length) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  (void)receiver;
  if (slot->connection == NULL || requested_length > UINT_MAX ||
      authentication_length > UINT_MAX || realm_length > UINT_MAX)
    return SASL_BADPARAM;
  return slot->native.authorize(slot->connection, slot->native_context,
                                requested, (unsigned)requested_length,
                                authentication, (unsigned)authentication_length,
                                realm, (unsigned)realm_length,
                                sasl_auxprop_getctx(slot->connection));
}
static int cpkt_sasl_selected_check_password(cpkt_sasl *receiver, void *context,
                                             const char *user,
                                             const char *password,
                                             unsigned long length) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  (void)receiver;
  if (slot->connection == NULL || length > UINT_MAX)
    return SASL_BADPARAM;
  return slot->native.check_password(slot->connection, slot->native_context,
                                     user, password, (unsigned)length,
                                     sasl_auxprop_getctx(slot->connection));
}
static int cpkt_sasl_selected_set_password(cpkt_sasl *receiver, void *context,
                                           const char *user,
                                           const char *password,
                                           unsigned long length,
                                           unsigned long flags) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  (void)receiver;
  if (slot->connection == NULL || length > UINT_MAX || flags > UINT_MAX)
    return SASL_BADPARAM;
  return slot->native.set_password(
      slot->connection, slot->native_context, user, password, (unsigned)length,
      sasl_auxprop_getctx(slot->connection), (unsigned)flags);
}
static int cpkt_sasl_selected_canonicalize(
    cpkt_sasl *receiver, void *context, const char *input, unsigned long length,
    unsigned long flags, const char *realm, char *output,
    unsigned long capacity, unsigned long *output_length) {
  cpkt_sasl_selected_callback *slot = (cpkt_sasl_selected_callback *)context;
  unsigned native_length = 0;
  int status;
  (void)receiver;
  if (slot->connection == NULL || length > UINT_MAX || flags > UINT_MAX ||
      capacity > UINT_MAX)
    return SASL_BADPARAM;
  status = slot->native.canonicalize(
      slot->connection, slot->native_context, input, (unsigned)length,
      (unsigned)flags, realm, output, (unsigned)capacity, &native_length);
  if (output_length != NULL)
    *output_length = native_length;
  return status;
}

static int cpkt_sasl_selected_index(unsigned long id) {
  switch (id) {
  case SASL_CB_GETOPT:
    return 0;
  case SASL_CB_LOG:
    return 1;
  case SASL_CB_GETPATH:
    return 2;
  case SASL_CB_VERIFYFILE:
    return 3;
  case SASL_CB_GETCONFPATH:
    return 4;
  case SASL_CB_USER:
    return 5;
  case SASL_CB_AUTHNAME:
    return 6;
  case SASL_CB_LANGUAGE:
    return 7;
  case SASL_CB_CNONCE:
    return 8;
  case SASL_CB_PASS:
    return 9;
  case SASL_CB_ECHOPROMPT:
    return 10;
  case SASL_CB_NOECHOPROMPT:
    return 11;
  case SASL_CB_GETREALM:
    return 12;
  case SASL_CB_PROXY_POLICY:
    return 13;
  case SASL_CB_SERVER_USERDB_CHECKPASS:
    return 14;
  case SASL_CB_SERVER_USERDB_SETPASS:
    return 15;
  case SASL_CB_CANON_USER:
    return 16;
  default:
    return -1;
  }
}

static void cpkt_sasl_selected_bind(cpkt_sasl_selected_callback *slot,
                                    const cpkt_sasl_callbacks *source) {
  cpkt_sasl_callbacks *out = &slot->callbacks;
  out->context = source == NULL ? slot : source->context;
#define CPKT_SASL_BIND(id, field, adapter)                                     \
  case id:                                                                     \
    out->field = source == NULL ? adapter : source->field;                     \
    break
  switch (slot->id) {
    CPKT_SASL_BIND(SASL_CB_GETOPT, option, cpkt_sasl_selected_option);
    CPKT_SASL_BIND(SASL_CB_LOG, log, cpkt_sasl_selected_log);
    CPKT_SASL_BIND(SASL_CB_GETPATH, plugin_path, cpkt_sasl_selected_path);
    CPKT_SASL_BIND(SASL_CB_VERIFYFILE, verify_file, cpkt_sasl_selected_verify);
    CPKT_SASL_BIND(SASL_CB_GETCONFPATH, configuration_path,
                   cpkt_sasl_selected_confpath);
    CPKT_SASL_BIND(SASL_CB_USER, simple, cpkt_sasl_selected_simple);
    CPKT_SASL_BIND(SASL_CB_AUTHNAME, simple, cpkt_sasl_selected_simple);
    CPKT_SASL_BIND(SASL_CB_LANGUAGE, language, cpkt_sasl_selected_simple);
    CPKT_SASL_BIND(SASL_CB_CNONCE, client_nonce, cpkt_sasl_selected_simple);
    CPKT_SASL_BIND(SASL_CB_PASS, secret, cpkt_sasl_selected_secret);
    CPKT_SASL_BIND(SASL_CB_ECHOPROMPT, challenge, cpkt_sasl_selected_challenge);
    CPKT_SASL_BIND(SASL_CB_NOECHOPROMPT, challenge_no_echo,
                   cpkt_sasl_selected_challenge);
    CPKT_SASL_BIND(SASL_CB_GETREALM, realm, cpkt_sasl_selected_realm);
    CPKT_SASL_BIND(SASL_CB_PROXY_POLICY, authorize,
                   cpkt_sasl_selected_authorize);
    CPKT_SASL_BIND(SASL_CB_SERVER_USERDB_CHECKPASS, check_password,
                   cpkt_sasl_selected_check_password);
    CPKT_SASL_BIND(SASL_CB_SERVER_USERDB_SETPASS, set_password,
                   cpkt_sasl_selected_set_password);
    CPKT_SASL_BIND(SASL_CB_CANON_USER, canonicalize,
                   cpkt_sasl_selected_canonicalize);
  }
#undef CPKT_SASL_BIND
}

static int
cpkt_sasl_plugin_get_callbacks(const cpkt_sasl_plugin_utils *self,
                               unsigned long callback_id,
                               const cpkt_sasl_callbacks **callbacks_out) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  sasl_callback_ft callback = NULL;
  void *context = NULL;
  cpkt_sasl_utils_entry *entry;
  cpkt_sasl_selected_callback *slot;
  const cpkt_sasl_callbacks *source;
  int index = cpkt_sasl_selected_index(callback_id);
  int status;
  if (callbacks_out != NULL)
    *callbacks_out = NULL;
  if (native == NULL || native->getcallback == NULL || callbacks_out == NULL ||
      index < 0)
    return SASL_BADPARAM;
  status = native->getcallback(native->conn, callback_id, &callback, &context);
  if (status != SASL_OK)
    return status;
  if (callback == NULL)
    return SASL_INTERACT;
  source = cpkt_sasl_callbacks_from_native_context(context);
  (void)pthread_mutex_lock(&cpkt_sasl_utils_lock);
  for (entry = cpkt_sasl_utils_entries; entry != NULL; entry = entry->next)
    if (&entry->public_utils == self)
      break;
  if (entry == NULL) {
    (void)pthread_mutex_unlock(&cpkt_sasl_utils_lock);
    return SASL_FAIL;
  }
  slot = &entry->selected[index];
  memset(slot, 0, sizeof(*slot));
  slot->id = callback_id;
  slot->native.generic = callback;
  slot->native_context = context;
  slot->connection = native->conn;
  cpkt_sasl_selected_bind(slot, source);
  *callbacks_out = &slot->callbacks;
  (void)pthread_mutex_unlock(&cpkt_sasl_utils_lock);
  return SASL_OK;
}
static void cpkt_sasl_plugin_log(const cpkt_sasl_plugin_utils *self, int level,
                                 const char *message) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native != NULL && native->log != NULL && message != NULL)
    native->log(native->conn, level, "%s", message);
}
static void cpkt_sasl_plugin_set_error(const cpkt_sasl_plugin_utils *self,
                                       unsigned long flags,
                                       const char *message) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native != NULL && native->seterror != NULL && flags <= UINT_MAX &&
      message != NULL)
    native->seterror(native->conn, (unsigned)flags, "%s", message);
}
static void cpkt_sasl_plugin_erase(const cpkt_sasl_plugin_utils *self,
                                   char *bytes, unsigned long length) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native == NULL || native->erasebuffer == NULL)
    return;
  while (length != 0) {
    unsigned chunk = length > UINT_MAX ? UINT_MAX : (unsigned)length;
    native->erasebuffer(bytes, chunk);
    bytes += chunk;
    length -= chunk;
  }
}

static cpkt_sasl_property_context *
cpkt_sasl_plugin_property_new(const cpkt_sasl_plugin_utils *self,
                              unsigned long estimate) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  cpkt_sasl_property_context *ctx;
  if (native == NULL || native->prop_new == NULL || estimate > UINT_MAX)
    return NULL;
  ctx = (cpkt_sasl_property_context *)calloc(1, sizeof(*ctx));
  if (ctx == NULL)
    return NULL;
  ctx->native = native->prop_new((unsigned)estimate);
  if (ctx->native == NULL) {
    free(ctx);
    return NULL;
  }
  return ctx;
}
static int
cpkt_sasl_plugin_property_duplicate(const cpkt_sasl_plugin_utils *self,
                                    cpkt_sasl_property_context *source,
                                    cpkt_sasl_property_context **copy_out) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  cpkt_sasl_property_context *copy;
  int status;
  if (native == NULL || native->prop_dup == NULL || source == NULL ||
      copy_out == NULL)
    return SASL_BADPARAM;
  *copy_out = NULL;
  copy = (cpkt_sasl_property_context *)calloc(1, sizeof(*copy));
  if (copy == NULL)
    return SASL_NOMEM;
  status = native->prop_dup(source->native, &copy->native);
  if (status != SASL_OK) {
    free(copy);
    return status;
  }
  *copy_out = copy;
  return SASL_OK;
}
static int
cpkt_sasl_plugin_property_request(const cpkt_sasl_plugin_utils *self,
                                  cpkt_sasl_property_context *context,
                                  const char *const *names) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native == NULL || native->prop_request == NULL || context == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_property_discard_borrowed_views(context);
  return native->prop_request(context->native, (const char **)names);
}
static const cpkt_sasl_property_value *
cpkt_sasl_plugin_property_get(const cpkt_sasl_plugin_utils *self,
                              cpkt_sasl_property_context *context,
                              size_t *count_out) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native == NULL || native->prop_get == NULL)
    return NULL;
  return cpkt_sasl_property_get(context, count_out);
}
static int cpkt_sasl_plugin_property_getnames(
    const cpkt_sasl_plugin_utils *self, cpkt_sasl_property_context *context,
    const char *const *names, cpkt_sasl_property_value *values_out,
    size_t capacity, size_t *count_out) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native == NULL || native->prop_getnames == NULL)
    return SASL_BADPARAM;
  return cpkt_sasl_property_getnames(context, names, values_out, capacity,
                                     count_out);
}
static void cpkt_sasl_plugin_property_clear(const cpkt_sasl_plugin_utils *self,
                                            cpkt_sasl_property_context *context,
                                            int clear_requests) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native != NULL && native->prop_clear != NULL && context != NULL) {
    cpkt_sasl_property_discard_borrowed_views(context);
    native->prop_clear(context->native, clear_requests);
  }
}
static void
cpkt_sasl_plugin_property_dispose(const cpkt_sasl_plugin_utils *self,
                                  cpkt_sasl_property_context **context) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native != NULL && native->prop_dispose != NULL && context != NULL &&
      *context != NULL && !(*context)->borrowed) {
    cpkt_sasl_property_discard_borrowed_views(*context);
    native->prop_dispose(&(*context)->native);
    free(*context);
    *context = NULL;
  }
}
static int cpkt_sasl_plugin_property_format(const cpkt_sasl_plugin_utils *self,
                                            cpkt_sasl_property_context *context,
                                            const char *separator,
                                            int separator_length, char *output,
                                            unsigned long capacity,
                                            unsigned long *length_out) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  unsigned native_length = 0;
  int status;
  if (length_out != NULL)
    *length_out = 0;
  if (native == NULL || native->prop_format == NULL || context == NULL ||
      capacity > UINT_MAX)
    return SASL_BADPARAM;
  status = native->prop_format(context->native, separator, separator_length,
                               output, (unsigned)capacity, &native_length);
  if (length_out != NULL)
    *length_out = native_length;
  return status;
}
static int cpkt_sasl_plugin_property_set(const cpkt_sasl_plugin_utils *self,
                                         cpkt_sasl_property_context *context,
                                         const char *name, const char *value,
                                         int value_length) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native == NULL || native->prop_set == NULL || context == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_property_discard_borrowed_views(context);
  return native->prop_set(context->native, name, value, value_length);
}
static int cpkt_sasl_plugin_property_set_values(
    const cpkt_sasl_plugin_utils *self, cpkt_sasl_property_context *context,
    const char *name, const char *const *values) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native == NULL || native->prop_setvals == NULL || context == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_property_discard_borrowed_views(context);
  return native->prop_setvals(context->native, name, (const char **)values);
}
static void cpkt_sasl_plugin_property_erase(const cpkt_sasl_plugin_utils *self,
                                            cpkt_sasl_property_context *context,
                                            const char *name) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  if (native != NULL && native->prop_erase != NULL && context != NULL) {
    cpkt_sasl_property_discard_borrowed_views(context);
    native->prop_erase(context->native, name);
  }
}
static int cpkt_sasl_plugin_auxiliary_store(const cpkt_sasl_plugin_utils *self,
                                            cpkt_sasl_property_context *context,
                                            const char *user) {
  const sasl_utils_t *native = cpkt_sasl_native_utils(self);
  return native == NULL || native->auxprop_store == NULL
             ? SASL_BADPARAM
             : native->auxprop_store(native->conn,
                                     context == NULL ? NULL : context->native,
                                     user);
}

/** C89 facade contract for cpkt_sasl_plugin_utils_initialize; see the public
 * header for ownership and callback lifetime. */
void cpkt_sasl_plugin_utils_initialize(cpkt_sasl_plugin_utils *public_utils,
                                       const sasl_utils_t *native) {
  const cpkt_sasl_callbacks *selected;
  memset(public_utils, 0, sizeof(*public_utils));
  if (native == NULL)
    return;
  /* The pinned Cyrus allocator leaves sasl_utils_t.version uninitialized.
   * The enabled layout is fixed by the public SASL_UTILS_VERSION contract. */
  public_utils->version = SASL_UTILS_VERSION;
  public_utils->connection = cpkt_sasl_public_for_native(native->conn);
  public_utils->random_pool = (cpkt_sasl_random *)native->rpool;
  selected = cpkt_sasl_callbacks_from_native_context(native->getopt_context);
  public_utils->option_context =
      selected != NULL ? selected->context
                       : cpkt_sasl_option_application_context(
                             cpkt_sasl_public_for_native(native->conn));
  public_utils->option =
      native->getopt == NULL ? NULL : cpkt_sasl_plugin_option;
  public_utils->allocate = cpkt_sasl_plugin_allocate;
  public_utils->allocate_zeroed = cpkt_sasl_plugin_allocate_zeroed;
  public_utils->resize = cpkt_sasl_plugin_resize;
  public_utils->release = cpkt_sasl_plugin_release;
  public_utils->mutex_new = cpkt_sasl_plugin_mutex_new;
  public_utils->mutex_lock = cpkt_sasl_plugin_mutex_lock;
  public_utils->mutex_unlock = cpkt_sasl_plugin_mutex_unlock;
  public_utils->mutex_free = cpkt_sasl_plugin_mutex_free;
  public_utils->md5_init = cpkt_sasl_plugin_md5_init;
  public_utils->md5_update = cpkt_sasl_plugin_md5_update;
  public_utils->md5_final = cpkt_sasl_plugin_md5_final;
  public_utils->hmac_md5 = cpkt_sasl_plugin_hmac_md5;
  public_utils->hmac_md5_init = cpkt_sasl_plugin_hmac_init;
  public_utils->hmac_md5_update = cpkt_sasl_plugin_hmac_update;
  public_utils->hmac_md5_final = cpkt_sasl_plugin_hmac_final;
  public_utils->hmac_md5_precalculate = cpkt_sasl_plugin_hmac_precalculate;
  public_utils->hmac_md5_import = cpkt_sasl_plugin_hmac_import;
  public_utils->make_challenge = cpkt_sasl_plugin_make_challenge;
  public_utils->utf8_verify = cpkt_sasl_plugin_utf8_verify;
  public_utils->random_fill = cpkt_sasl_plugin_random_fill;
  public_utils->random_churn = cpkt_sasl_plugin_random_churn;
  public_utils->check_password = cpkt_sasl_plugin_check_password;
  public_utils->base64_decode = cpkt_sasl_plugin_base64_decode;
  public_utils->base64_encode = cpkt_sasl_plugin_base64_encode;
  public_utils->get_text_property = cpkt_sasl_plugin_get_text_property;
  public_utils->get_number_property = cpkt_sasl_plugin_get_number_property;
  public_utils->set_text_property = cpkt_sasl_plugin_set_text_property;
  public_utils->set_external_ssf = cpkt_sasl_plugin_set_external_ssf;
  public_utils->set_external_authentication =
      cpkt_sasl_plugin_set_external_authentication;
  public_utils->get_callbacks = cpkt_sasl_plugin_get_callbacks;
  public_utils->log = cpkt_sasl_plugin_log;
  public_utils->set_error = cpkt_sasl_plugin_set_error;
  public_utils->erase = cpkt_sasl_plugin_erase;
  public_utils->property_new = cpkt_sasl_plugin_property_new;
  public_utils->property_duplicate = cpkt_sasl_plugin_property_duplicate;
  public_utils->property_request = cpkt_sasl_plugin_property_request;
  public_utils->property_get = cpkt_sasl_plugin_property_get;
  public_utils->property_getnames = cpkt_sasl_plugin_property_getnames;
  public_utils->property_clear = cpkt_sasl_plugin_property_clear;
  public_utils->property_dispose = cpkt_sasl_plugin_property_dispose;
  public_utils->property_format = cpkt_sasl_plugin_property_format;
  public_utils->property_set = cpkt_sasl_plugin_property_set;
  public_utils->property_set_values = cpkt_sasl_plugin_property_set_values;
  public_utils->property_erase = cpkt_sasl_plugin_property_erase;
  public_utils->auxiliary_store = cpkt_sasl_plugin_auxiliary_store;
  public_utils->internal = (void *)native;
}
