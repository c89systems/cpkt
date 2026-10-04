#ifndef CPKT_SASL_PLUGIN_H
#define CPKT_SASL_PLUGIN_H

/** C89 custom Cyrus SASL plugin boundary. Records passed to add_plugin are
 * copied; callback contexts, names, and returned byte buffers stay owned by
 * the application until the matching SASL client/server finish. The bundled
 * digest functions implement the RSA Data Security, Inc. MD5 Message-Digest
 * Algorithm; this facade only calls the bundled implementation. */

#include <cpkt/gssapi.h>
#include <cpkt/sasl.h>

/** Provider utility table retained through mech_free for global plugins and
 * through connection disposal for connection callbacks. Its connection is a
 * borrowed receiver, null at global initialization. */
typedef struct cpkt_sasl_plugin_utils cpkt_sasl_plugin_utils;
/** C89 facade contract for cpkt_sasl_plugin_output; see the public header for
 * ownership and callback lifetime. */
typedef struct cpkt_sasl_plugin_output cpkt_sasl_plugin_output;
/** C89 facade contract for cpkt_sasl_client_params; see the public header for
 * ownership and callback lifetime. */
typedef struct cpkt_sasl_client_params cpkt_sasl_client_params;
/** C89 facade contract for cpkt_sasl_server_params; see the public header for
 * ownership and callback lifetime. */
typedef struct cpkt_sasl_server_params cpkt_sasl_server_params;

/** 32-bit word arrays have the native MD5 format on every shipped target.
 * Utility calls still copy fields to and from native contexts. */
typedef struct cpkt_sasl_md5_context {
  unsigned int state[4];
  unsigned int count[2];
  unsigned char buffer[64];
} cpkt_sasl_md5_context;
/** C89 facade contract for cpkt_sasl_hmac_md5_context; see the public header
 * for ownership and callback lifetime. */
typedef struct cpkt_sasl_hmac_md5_context {
  cpkt_sasl_md5_context inner;
  cpkt_sasl_md5_context outer;
} cpkt_sasl_hmac_md5_context;
/** C89 facade contract for cpkt_sasl_hmac_md5_state; see the public header for
 * ownership and callback lifetime. */
typedef struct cpkt_sasl_hmac_md5_state {
  unsigned int inner[4];
  unsigned int outer[4];
} cpkt_sasl_hmac_md5_state;

#define CPKT_SASL_HMAC_MD5_SIZE 16
/** Direct C89 bindings for the public Cyrus SASL MD5/HMAC helpers. Contexts
 * are caller-owned; finalization mutates them as the native helpers do. */
void cpkt_sasl_md5_initialize(cpkt_sasl_md5_context *context);
/** C89 facade contract for cpkt_sasl_md5_update; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_md5_update(cpkt_sasl_md5_context *context,
                         const unsigned char *bytes, unsigned long length);
/** C89 facade contract for cpkt_sasl_md5_final; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_md5_final(unsigned char digest[16],
                         cpkt_sasl_md5_context *context);
/** C89 facade contract for cpkt_sasl_hmac_md5; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_hmac_md5(const unsigned char *text, int text_length,
                        const unsigned char *key, int key_length,
                        unsigned char digest[16]);
/** C89 facade contract for cpkt_sasl_hmac_md5_initialize; see the public header
 * for ownership and callback lifetime. */
void cpkt_sasl_hmac_md5_initialize(cpkt_sasl_hmac_md5_context *context,
                                   const unsigned char *key, int key_length);
/** C89 facade contract for cpkt_sasl_hmac_md5_update; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_hmac_md5_update(cpkt_sasl_hmac_md5_context *context,
                              const unsigned char *bytes, unsigned long length);
/** C89 facade contract for cpkt_sasl_hmac_md5_final; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_hmac_md5_final(unsigned char digest[16],
                              cpkt_sasl_hmac_md5_context *context);
/** C89 facade contract for cpkt_sasl_hmac_md5_precalculate; see the public
 * header for ownership and callback lifetime. */
void cpkt_sasl_hmac_md5_precalculate(cpkt_sasl_hmac_md5_state *state,
                                     const unsigned char *key, int key_length);
/** C89 facade contract for cpkt_sasl_hmac_md5_import; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_hmac_md5_import(cpkt_sasl_hmac_md5_context *context,
                               const cpkt_sasl_hmac_md5_state *state);

/** Utility slots take the record as their first argument. The table identity
 * is stable for its native lifetime. option_context is the configured option
 * callback's application context when one exists; otherwise it is NULL.
 * Call option() for native configuration fallback. get_callbacks returns the
 * actual selected callback and context, or the provider's no-handler status. */
struct cpkt_sasl_plugin_utils {
  int version;
  cpkt_sasl *connection;
  cpkt_sasl_random *random_pool;
  void *option_context;
  int (*option)(const cpkt_sasl_plugin_utils *self, const char *plugin,
                const char *name, const char **result,
                unsigned long *result_length);
  void *(*allocate)(const cpkt_sasl_plugin_utils *self, size_t size);
  void *(*allocate_zeroed)(const cpkt_sasl_plugin_utils *self, size_t count,
                           size_t size);
  void *(*resize)(const cpkt_sasl_plugin_utils *self, void *value, size_t size);
  void (*release)(const cpkt_sasl_plugin_utils *self, void *value);
  void *(*mutex_new)(const cpkt_sasl_plugin_utils *self);
  int (*mutex_lock)(const cpkt_sasl_plugin_utils *self, void *mutex);
  int (*mutex_unlock)(const cpkt_sasl_plugin_utils *self, void *mutex);
  void (*mutex_free)(const cpkt_sasl_plugin_utils *self, void *mutex);
  void (*md5_init)(const cpkt_sasl_plugin_utils *self,
                   cpkt_sasl_md5_context *context);
  int (*md5_update)(const cpkt_sasl_plugin_utils *self,
                    cpkt_sasl_md5_context *context, const unsigned char *bytes,
                    unsigned long length);
  void (*md5_final)(const cpkt_sasl_plugin_utils *self,
                    unsigned char digest[16], cpkt_sasl_md5_context *context);
  void (*hmac_md5)(const cpkt_sasl_plugin_utils *self,
                   const unsigned char *text, int text_length,
                   const unsigned char *key, int key_length,
                   unsigned char digest[16]);
  void (*hmac_md5_init)(const cpkt_sasl_plugin_utils *self,
                        cpkt_sasl_hmac_md5_context *context,
                        const unsigned char *key, int key_length);
  int (*hmac_md5_update)(const cpkt_sasl_plugin_utils *self,
                         cpkt_sasl_hmac_md5_context *context,
                         const unsigned char *bytes, unsigned long length);
  void (*hmac_md5_final)(const cpkt_sasl_plugin_utils *self,
                         unsigned char digest[16],
                         cpkt_sasl_hmac_md5_context *context);
  void (*hmac_md5_precalculate)(const cpkt_sasl_plugin_utils *self,
                                cpkt_sasl_hmac_md5_state *state,
                                const unsigned char *key, int key_length);
  void (*hmac_md5_import)(const cpkt_sasl_plugin_utils *self,
                          cpkt_sasl_hmac_md5_context *context,
                          const cpkt_sasl_hmac_md5_state *state);
  int (*make_challenge)(const cpkt_sasl_plugin_utils *self, char *output,
                        unsigned long capacity, unsigned long host_flag);
  int (*utf8_verify)(const cpkt_sasl_plugin_utils *self, const char *text,
                     unsigned long length);
  int (*random_fill)(const cpkt_sasl_plugin_utils *self, char *output,
                     unsigned long length);
  int (*random_churn)(const cpkt_sasl_plugin_utils *self, const char *bytes,
                      unsigned long length);
  int (*check_password)(const cpkt_sasl_plugin_utils *self, const char *user,
                        unsigned long user_length, const char *password,
                        unsigned long password_length);
  int (*base64_decode)(const cpkt_sasl_plugin_utils *self, const char *input,
                       unsigned long input_length, char *output,
                       unsigned long capacity, unsigned long *output_length);
  int (*base64_encode)(const cpkt_sasl_plugin_utils *self, const char *input,
                       unsigned long input_length, char *output,
                       unsigned long capacity, unsigned long *output_length);
  int (*get_text_property)(const cpkt_sasl_plugin_utils *self, int property,
                           const char **value_out);
  int (*get_number_property)(const cpkt_sasl_plugin_utils *self, int property,
                             unsigned long *value_out);
  /** Selected connection property setters; for nested records and callback
   * views use the borrowed connection receiver's typed methods. */
  int (*set_text_property)(const cpkt_sasl_plugin_utils *self, int property,
                           const char *value);
  int (*set_external_ssf)(const cpkt_sasl_plugin_utils *self,
                          unsigned long value);
  int (*set_external_authentication)(const cpkt_sasl_plugin_utils *self,
                                     const char *identity);
  int (*get_callbacks)(const cpkt_sasl_plugin_utils *self,
                       unsigned long callback_id,
                       const cpkt_sasl_callbacks **callbacks_out);
  void (*log)(const cpkt_sasl_plugin_utils *self, int level,
              const char *message);
  void (*set_error)(const cpkt_sasl_plugin_utils *self, unsigned long flags,
                    const char *message);
  void (*erase)(const cpkt_sasl_plugin_utils *self, char *bytes,
                unsigned long length);
  cpkt_sasl_property_context *(*property_new)(
      const cpkt_sasl_plugin_utils *self, unsigned long estimate);
  int (*property_duplicate)(const cpkt_sasl_plugin_utils *self,
                            cpkt_sasl_property_context *source,
                            cpkt_sasl_property_context **copy_out);
  int (*property_request)(const cpkt_sasl_plugin_utils *self,
                          cpkt_sasl_property_context *context,
                          const char *const *names);
  const cpkt_sasl_property_value *(*property_get)(
      const cpkt_sasl_plugin_utils *self, cpkt_sasl_property_context *context,
      size_t *count_out);
  int (*property_getnames)(const cpkt_sasl_plugin_utils *self,
                           cpkt_sasl_property_context *context,
                           const char *const *names,
                           cpkt_sasl_property_value *values_out,
                           size_t capacity, size_t *count_out);
  void (*property_clear)(const cpkt_sasl_plugin_utils *self,
                         cpkt_sasl_property_context *context,
                         int clear_requests);
  void (*property_dispose)(const cpkt_sasl_plugin_utils *self,
                           cpkt_sasl_property_context **context);
  int (*property_format)(const cpkt_sasl_plugin_utils *self,
                         cpkt_sasl_property_context *context,
                         const char *separator, int separator_length,
                         char *output, unsigned long capacity,
                         unsigned long *length_out);
  int (*property_set)(const cpkt_sasl_plugin_utils *self,
                      cpkt_sasl_property_context *context, const char *name,
                      const char *value, int value_length);
  int (*property_set_values)(const cpkt_sasl_plugin_utils *self,
                             cpkt_sasl_property_context *context,
                             const char *name, const char *const *values);
  void (*property_erase)(const cpkt_sasl_plugin_utils *self,
                         cpkt_sasl_property_context *context, const char *name);
  int (*auxiliary_store)(const cpkt_sasl_plugin_utils *self,
                         cpkt_sasl_property_context *context, const char *user);
  void *internal;
};

/** Mutable output fields from a mechanism; the facade converts each field
 * and retains callback bridges until the connection is disposed. */
struct cpkt_sasl_plugin_output {
  unsigned long done;
  const char *user;
  const char *authentication_identity;
  unsigned long user_length;
  unsigned long authentication_length;
  unsigned long maximum_output_bytes;
  unsigned long mechanism_ssf;
  void *encode_context;
  int (*encode)(cpkt_sasl_plugin_output *self, const cpkt_sasl_iov *vectors,
                size_t count, const char **output,
                unsigned long *output_length);
  void *decode_context;
  int (*decode)(cpkt_sasl_plugin_output *self, const char *input,
                unsigned long input_length, const char **output,
                unsigned long *output_length);
  /** Native mechanism-owned payload. GSS/GS2 use a pointer to a credential
   * cell; custom mechanisms may use any application-defined payload. */
  /** Mechanism-specific borrowed payload. GSS plugins place a pointer to a
   * GSS credential handle cell here; other plugins may use their own type. */
  void *client_credentials;
  const cpkt_gss_name *gss_peer_name;
  const cpkt_gss_name *gss_local_name;
  const char *channel_binding_name;
  unsigned long channel_binding_disposition;
  int parameter_version;
  void *internal;
};

struct cpkt_sasl_client_params {
  const char *service;
  const char *server_name;
  const char *client_name;
  const char *local_endpoint;
  const char *remote_endpoint;
  /** Borrowed application prompt callback record for this connection. */
  const cpkt_sasl_callbacks *prompt_callbacks;
  unsigned long service_length;
  unsigned long server_length;
  unsigned long client_length;
  unsigned long local_length;
  unsigned long remote_length;
  cpkt_sasl_security_properties security;
  unsigned long external_ssf;
  const cpkt_gss_credential *gss_credentials;
  const cpkt_sasl_channel_binding *channel_binding;
  const cpkt_sasl_http_request *http_request;
  unsigned long channel_binding_disposition;
  unsigned long flags;
  int parameter_version;
  const cpkt_sasl_plugin_utils *utils;
  int (*canonicalize)(cpkt_sasl_client_params *self, const char *input,
                      unsigned long length, unsigned long flags,
                      cpkt_sasl_plugin_output *output);
  void *internal;
};

struct cpkt_sasl_server_params {
  const char *service;
  const char *application_name;
  const char *server_name;
  const char *user_realm;
  const char *local_endpoint;
  const char *remote_endpoint;
  /** Borrowed application callback record for this connection. */
  const cpkt_sasl_callbacks *callbacks;
  unsigned long service_length;
  unsigned long application_length;
  unsigned long server_length;
  unsigned long realm_length;
  unsigned long local_length;
  unsigned long remote_length;
  int log_level;
  cpkt_sasl_security_properties security;
  unsigned long external_ssf;
  cpkt_sasl_property_context *properties;
  const cpkt_gss_credential *gss_credentials;
  const cpkt_sasl_channel_binding *channel_binding;
  const cpkt_sasl_http_request *http_request;
  unsigned long flags;
  int parameter_version;
  const cpkt_sasl_plugin_utils *utils;
  int (*transition)(cpkt_sasl_server_params *self, const char *password,
                    unsigned long length);
  int (*canonicalize)(cpkt_sasl_server_params *self, const char *input,
                      unsigned long length, unsigned long flags,
                      cpkt_sasl_plugin_output *output);
  void *internal;
};

/** Zero-initialize optional callbacks. Global and connection callback
 * contexts remain caller-owned until the matching SASL finish. */
typedef struct cpkt_sasl_client_plugin {
  const char *mechanism_name;
  unsigned long maximum_ssf;
  unsigned long security_flags;
  unsigned long features;
  const unsigned long *required_prompts;
  void *context;
  int (*new_connection)(void *context, cpkt_sasl_client_params *params,
                        void **connection_context_out);
  int (*step)(void *connection_context, cpkt_sasl_client_params *params,
              const char *server_input, unsigned long input_length,
              cpkt_sasl_interaction **interaction_out,
              const char **client_output, unsigned long *output_length,
              cpkt_sasl_plugin_output *output_params);
  void (*dispose_connection)(void *connection_context,
                             const cpkt_sasl_plugin_utils *utils);
  void (*free_context)(void *context, const cpkt_sasl_plugin_utils *utils);
  /** Borrowed public receiver, not the mechanism's new_connection state.
   * Global client precomputation supplies null connection and params. */
  int (*idle)(void *context, cpkt_sasl *connection,
              cpkt_sasl_client_params *params);
} cpkt_sasl_client_plugin;

/** C89 facade contract for cpkt_sasl_server_plugin; see the public header for
 * ownership and callback lifetime. */
typedef struct cpkt_sasl_server_plugin {
  const char *mechanism_name;
  unsigned long maximum_ssf;
  unsigned long security_flags;
  unsigned long features;
  void *context;
  int (*new_connection)(void *context, cpkt_sasl_server_params *params,
                        const char *challenge, unsigned long challenge_length,
                        void **connection_context_out);
  int (*step)(void *connection_context, cpkt_sasl_server_params *params,
              const char *client_input, unsigned long input_length,
              const char **server_output, unsigned long *output_length,
              cpkt_sasl_plugin_output *output_params);
  void (*dispose_connection)(void *connection_context,
                             const cpkt_sasl_plugin_utils *utils);
  void (*free_context)(void *context, const cpkt_sasl_plugin_utils *utils);
  int (*set_password)(void *context, cpkt_sasl_server_params *params,
                      const char *user, const char *password,
                      unsigned long password_length, const char *old_password,
                      unsigned long old_password_length, unsigned long flags);
  int (*user_query)(void *context, cpkt_sasl_server_params *params,
                    const char *user, int maximum_mechanisms,
                    const char **mechanisms_out);
  /** Borrowed public receiver passed by the native server idle hook. */
  int (*idle)(void *context, cpkt_sasl *connection,
              cpkt_sasl_server_params *params);
  int (*available)(void *context, cpkt_sasl_server_params *params,
                   void **connection_context_out);
} cpkt_sasl_server_plugin;

/** C89 facade contract for cpkt_sasl_canonicalizer_plugin; see the public
 * header for ownership and callback lifetime. */
typedef struct cpkt_sasl_canonicalizer_plugin {
  int features;
  void *context;
  const char *name;
  void (*free_context)(void *context, const cpkt_sasl_plugin_utils *utils);
  int (*server)(void *context, cpkt_sasl_server_params *params,
                const char *user, unsigned long user_length,
                unsigned long flags, char *output, unsigned long capacity,
                unsigned long *length_out);
  int (*client)(void *context, cpkt_sasl_client_params *params,
                const char *user, unsigned long user_length,
                unsigned long flags, char *output, unsigned long capacity,
                unsigned long *length_out);
} cpkt_sasl_canonicalizer_plugin;

/** C89 facade contract for cpkt_sasl_auxiliary_plugin; see the public header
 * for ownership and callback lifetime. */
typedef struct cpkt_sasl_auxiliary_plugin {
  int features;
  void *context;
  const char *name;
  void (*free_context)(void *context, const cpkt_sasl_plugin_utils *utils);
  int (*lookup)(void *context, cpkt_sasl_server_params *params,
                unsigned long flags, const char *user,
                unsigned long user_length);
  int (*store)(void *context, cpkt_sasl_server_params *params,
               cpkt_sasl_property_context *properties, const char *user,
               unsigned long user_length);
} cpkt_sasl_auxiliary_plugin;

#define CPKT_SASL_UTILS_VERSION 4
#define CPKT_SASL_CLIENT_PLUGIN_VERSION 4
#define CPKT_SASL_SERVER_PLUGIN_VERSION 4
#define CPKT_SASL_CANONICALIZER_PLUGIN_VERSION 5
#define CPKT_SASL_AUXILIARY_PLUGIN_VERSION 8
#define CPKT_SASL_FEATURE_WANT_CLIENT_FIRST 2UL
#define CPKT_SASL_FEATURE_WANT_SERVER_LAST 4UL
#define CPKT_SASL_FEATURE_INTERNAL_CLIENT_FIRST 8UL
#define CPKT_SASL_FEATURE_SERVER_FIRST 16UL
#define CPKT_SASL_FEATURE_ALLOWS_PROXY 32UL
#define CPKT_SASL_FEATURE_DONT_USE_USER_PASSWORD 128UL
#define CPKT_SASL_FEATURE_GSS_FRAMING 256UL
#define CPKT_SASL_FEATURE_SERVICE 512UL
#define CPKT_SASL_FEATURE_GET_SECRET 1024UL
#define CPKT_SASL_FEATURE_CHANNEL_BINDING 2048UL
#define CPKT_SASL_FEATURE_SUPPORTS_HTTP 4096UL
#define CPKT_SASL_FEATURE_NEEDS_SERVER_NAME 1UL
#define CPKT_SASL_AUXILIARY_OVERRIDE 1UL
#define CPKT_SASL_AUXILIARY_AUTHORIZATION_ID 2UL
#define CPKT_SASL_AUXILIARY_VERIFY_HASH 16UL
#define CPKT_SASL_CHANNEL_BINDING_NONE 0
#define CPKT_SASL_CHANNEL_BINDING_WANTED 1
#define CPKT_SASL_CHANNEL_BINDING_USED 2
#define CPKT_SASL_CHANNEL_BINDING_PRESENT(params)                              \
  ((params)->channel_binding != NULL)
#define CPKT_SASL_CHANNEL_BINDING_CRITICAL(params)                             \
  (CPKT_SASL_CHANNEL_BINDING_PRESENT(params) &&                                \
   (params)->channel_binding->critical)
#define CPKT_SASL_INFO_LIST_START 0
#define CPKT_SASL_INFO_LIST_MECHANISM 1
#define CPKT_SASL_INFO_LIST_END 2
#define CPKT_SASL_CALLBACK_LIST_END 0UL
#define CPKT_SASL_CALLBACK_OPTION 1UL
#define CPKT_SASL_CALLBACK_LOG 2UL
#define CPKT_SASL_CALLBACK_PLUGIN_PATH 3UL
#define CPKT_SASL_CALLBACK_VERIFY_FILE 4UL
#define CPKT_SASL_CALLBACK_CONFIGURATION_PATH 5UL
#define CPKT_SASL_CALLBACK_USER 0x4001UL
#define CPKT_SASL_CALLBACK_AUTHENTICATION_NAME 0x4002UL
#define CPKT_SASL_CALLBACK_LANGUAGE 0x4003UL
#define CPKT_SASL_CALLBACK_PASSWORD 0x4004UL
#define CPKT_SASL_CALLBACK_ECHO_PROMPT 0x4005UL
#define CPKT_SASL_CALLBACK_NO_ECHO_PROMPT 0x4006UL
#define CPKT_SASL_CALLBACK_CLIENT_NONCE 0x4007UL
#define CPKT_SASL_CALLBACK_REALM 0x4008UL
#define CPKT_SASL_CALLBACK_PROXY_POLICY 0x8001UL
#define CPKT_SASL_CALLBACK_CHECK_PASSWORD 0x8005UL
#define CPKT_SASL_CALLBACK_SET_PASSWORD 0x8006UL
#define CPKT_SASL_CALLBACK_CANONICALIZE 0x8007UL

/** Views and strings are borrowed for the duration of the callback only.
 * A null view denotes a list boundary. */
typedef struct cpkt_sasl_plugin_info {
  int version;
  int condition;
  const char *plugin_name;
  const char *mechanism_name;
  const char *source_file;
  unsigned long maximum_ssf;
  unsigned long security_flags;
  unsigned long features;
  const unsigned long *required_prompts;
  /** Borrowed complete record for mechanisms registered through this facade.
   * Null for provider built-ins, whose callback ABI is native-only. */
  const cpkt_sasl_client_plugin *client_plugin;
  const cpkt_sasl_server_plugin *server_plugin;
  const cpkt_sasl_auxiliary_plugin *auxiliary_plugin;
} cpkt_sasl_plugin_info;
/** C89 facade contract for cpkt_sasl_plugin_info_callback; see the public
 * header for ownership and callback lifetime. */
typedef void (*cpkt_sasl_plugin_info_callback)(
    void *context, const cpkt_sasl_plugin_info *info, int stage);

/** C89 facade contract for cpkt_sasl_client_plugin_info; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_client_plugin_info(const char *mechanisms,
                                 cpkt_sasl_plugin_info_callback callback,
                                 void *context);
/** C89 facade contract for cpkt_sasl_server_plugin_info; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_server_plugin_info(const char *mechanisms,
                                 cpkt_sasl_plugin_info_callback callback,
                                 void *context);
/** C89 facade contract for cpkt_sasl_auxiliary_plugin_info; see the public
 * header for ownership and callback lifetime. */
int cpkt_sasl_auxiliary_plugin_info(const char *mechanisms,
                                    cpkt_sasl_plugin_info_callback callback,
                                    void *context);

/** C89 facade contract for cpkt_sasl_client_plugin_init; see the public header
 * for ownership and callback lifetime. */
typedef int (*cpkt_sasl_client_plugin_init)(
    void *context, const cpkt_sasl_plugin_utils *utils, int max_version,
    int *version_out, const cpkt_sasl_client_plugin **plugins_out,
    int *count_out);
/** C89 facade contract for cpkt_sasl_server_plugin_init; see the public header
 * for ownership and callback lifetime. */
typedef int (*cpkt_sasl_server_plugin_init)(
    void *context, const cpkt_sasl_plugin_utils *utils, int max_version,
    int *version_out, const cpkt_sasl_server_plugin **plugins_out,
    int *count_out);
/** C89 facade contract for cpkt_sasl_canonicalizer_plugin_init; see the public
 * header for ownership and callback lifetime. */
typedef int (*cpkt_sasl_canonicalizer_plugin_init)(
    void *context, const cpkt_sasl_plugin_utils *utils, int max_version,
    int *version_out, const cpkt_sasl_canonicalizer_plugin **plugin_out,
    const char *name);
/** C89 facade contract for cpkt_sasl_auxiliary_plugin_init; see the public
 * header for ownership and callback lifetime. */
typedef int (*cpkt_sasl_auxiliary_plugin_init)(
    void *context, const cpkt_sasl_plugin_utils *utils, int max_version,
    int *version_out, const cpkt_sasl_auxiliary_plugin **plugin_out,
    const char *name);

/** C89 facade contract for cpkt_sasl_client_add_plugin; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_client_add_plugin(const char *name,
                                cpkt_sasl_client_plugin_init initialize,
                                void *context);
/** C89 facade contract for cpkt_sasl_server_add_plugin; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_server_add_plugin(const char *name,
                                cpkt_sasl_server_plugin_init initialize,
                                void *context);
/** C89 facade contract for cpkt_sasl_canonicalizer_add_plugin; see the public
 * header for ownership and callback lifetime. */
int cpkt_sasl_canonicalizer_add_plugin(
    const char *name, cpkt_sasl_canonicalizer_plugin_init initialize,
    void *context);
/** C89 facade contract for cpkt_sasl_auxiliary_add_plugin; see the public
 * header for ownership and callback lifetime. */
int cpkt_sasl_auxiliary_add_plugin(const char *name,
                                   cpkt_sasl_auxiliary_plugin_init initialize,
                                   void *context);

#endif
