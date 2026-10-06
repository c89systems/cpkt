#ifndef CPKT_SASL_H
#define CPKT_SASL_H

/**
 * @defgroup cpkt_sasl Cyrus SASL C89 facade
 *
 * Each receiver owns one SASL connection; close it after borrowed output and
 * interaction views are no longer needed. Callback records are copied, while
 * their context pointers remain caller-owned. See
 * docs/sasl-c89-facade-spec.md for callback and interaction lifetimes.
 * @{
 */

#include <cpkt/gssapi.h>
#include <stddef.h>

/** Receiver shell for one SASL connection; close with self->close(). */
typedef struct cpkt_sasl cpkt_sasl;
typedef struct cpkt_sasl_property_context cpkt_sasl_property_context;
typedef struct cpkt_sasl_random cpkt_sasl_random;

/** Names and values are borrowed until the native property context is mutated
 * or disposed. Repeated get/getnames calls without mutation leave an existing
 * get view valid. A later get refreshes metadata after mutation through any
 * wrapper or the native provider. */
typedef struct cpkt_sasl_property_value {
  const char *name;
  const char *const *values;
  unsigned long value_count;
  unsigned long total_value_bytes;
} cpkt_sasl_property_value;

typedef void *(*cpkt_sasl_malloc_callback)(size_t);
typedef void *(*cpkt_sasl_calloc_callback)(size_t, size_t);
typedef void *(*cpkt_sasl_realloc_callback)(void *, size_t);
typedef void (*cpkt_sasl_free_callback)(void *);
typedef void *(*cpkt_sasl_mutex_alloc_callback)(void);
typedef int (*cpkt_sasl_mutex_lock_callback)(void *);
typedef int (*cpkt_sasl_mutex_unlock_callback)(void *);
typedef void (*cpkt_sasl_mutex_free_callback)(void *);

/** Connection security bounds, flags, and allowed property names. */
typedef struct cpkt_sasl_security_properties {
  unsigned long minimum_ssf;
  unsigned long maximum_ssf;
  unsigned long maximum_buffer_bytes;
  unsigned long security_flags;
  const char *const *property_names;
  const char *const *property_values;
} cpkt_sasl_security_properties;

/** Borrowed channel-binding bytes used while configuring a connection. */
typedef struct cpkt_sasl_channel_binding {
  const char *name;
  int critical;
  unsigned long byte_count;
  const unsigned char *data;
} cpkt_sasl_channel_binding;

/** Borrowed HTTP request fields for the HTTP SASL mechanism. */
typedef struct cpkt_sasl_http_request {
  const char *method;
  const char *uri;
  const unsigned char *entity;
  unsigned long entity_byte_count;
  unsigned long non_persistent;
} cpkt_sasl_http_request;

/** Borrowed vector element. Each byte range remains caller-owned for the
 * duration of encode_vector(). */
typedef struct cpkt_sasl_iov {
  const void *data;
  size_t byte_count;
} cpkt_sasl_iov;

/** Borrowed interaction fields to fill before retrying the same start/step
 * call. Lists end with CPKT_SASL_CALLBACK_LIST_END; only the terminator's id
 * is meaningful. */
typedef struct cpkt_sasl_interaction {
  unsigned long id;
  const char *challenge;
  const char *prompt;
  const char *default_result;
  const void *result;
  unsigned long result_byte_count;
} cpkt_sasl_interaction;

/** Password bytes are borrowed for the callback. The facade copies them into
 * receiver-owned storage before returning to the native client. */
typedef struct cpkt_sasl_secret {
  const unsigned char *data;
  unsigned long byte_count;
} cpkt_sasl_secret;

/** Returns a borrowed plugin option value to the native provider. */
typedef int (*cpkt_sasl_option_callback)(void *context, const char *plugin_name,
                                         const char *option,
                                         const char **result,
                                         unsigned long *result_byte_count);
/** Receives one SASL diagnostic message with its provider severity. */
typedef int (*cpkt_sasl_log_callback)(void *context, int level,
                                      const char *message);
/** Returns a borrowed plugin-search path to the native provider. */
typedef int (*cpkt_sasl_path_callback)(void *context, const char **path);
/** Approves or rejects a SASL configuration or plugin file. */
typedef int (*cpkt_sasl_verify_file_callback)(void *context, const char *path,
                                              int type);
/** Returns a configuration-search path to the native provider. */
typedef int (*cpkt_sasl_configuration_path_callback)(void *context,
                                                     char **path_out);
/** Provides a borrowed identity or other simple credential value. */
typedef int (*cpkt_sasl_simple_callback)(void *context, int id,
                                         const char **result,
                                         unsigned long *result_byte_count);
/** Provides password bytes; the facade copies them before returning. */
typedef int (*cpkt_sasl_secret_callback)(cpkt_sasl *connection, void *context,
                                         int id,
                                         const cpkt_sasl_secret **secret_out);
/** Answers a mechanism challenge with a borrowed response. */
typedef int (*cpkt_sasl_challenge_callback)(void *context, int id,
                                            const char *challenge,
                                            const char *prompt,
                                            const char *default_result,
                                            const char **result,
                                            unsigned long *result_byte_count);
/** Selects one realm from a borrowed list of available realms. */
typedef int (*cpkt_sasl_realm_callback)(void *context, int id,
                                        const char *const *available_realms,
                                        const char **result);
/** Decides whether an authenticated identity may act as the requested user. */
typedef int (*cpkt_sasl_authorize_callback)(
    cpkt_sasl *connection, void *context, const char *requested_user,
    unsigned long requested_length, const char *authentication_identity,
    unsigned long authentication_length, const char *default_realm,
    unsigned long realm_length);
/** Verifies supplied password bytes for a user. */
typedef int (*cpkt_sasl_check_password_callback)(cpkt_sasl *connection,
                                                 void *context,
                                                 const char *user,
                                                 const char *password,
                                                 unsigned long password_length);
/** Changes or disables a user's mechanism password. */
typedef int (*cpkt_sasl_set_password_callback)(cpkt_sasl *connection,
                                               void *context, const char *user,
                                               const char *password,
                                               unsigned long password_length,
                                               unsigned long flags);
/** Writes a canonical identity into the supplied output buffer. */
typedef int (*cpkt_sasl_canonicalize_callback)(
    cpkt_sasl *connection, void *context, const char *input,
    unsigned long input_length, unsigned long flags, const char *realm,
    char *output, unsigned long output_capacity, unsigned long *output_length);

/** Zero-initialization installs no callbacks. The facade copies the record but
 * does not own context or data returned by callbacks. */
typedef struct cpkt_sasl_callbacks {
  void *context;
  cpkt_sasl_option_callback option;
  cpkt_sasl_log_callback log;
  cpkt_sasl_path_callback plugin_path;
  cpkt_sasl_verify_file_callback verify_file;
  cpkt_sasl_configuration_path_callback configuration_path;
  cpkt_sasl_simple_callback simple;
  cpkt_sasl_simple_callback language;
  cpkt_sasl_simple_callback client_nonce;
  cpkt_sasl_secret_callback secret;
  cpkt_sasl_challenge_callback challenge;
  cpkt_sasl_challenge_callback challenge_no_echo;
  cpkt_sasl_realm_callback realm;
  cpkt_sasl_authorize_callback authorize;
  cpkt_sasl_check_password_callback check_password;
  cpkt_sasl_set_password_callback set_password;
  cpkt_sasl_canonicalize_callback canonicalize;
} cpkt_sasl_callbacks;

enum cpkt_sasl_constants {
  CPKT_SASL_OK = 0,
  CPKT_SASL_CONTINUE = 1,
  CPKT_SASL_INTERACT = 2,
  CPKT_SASL_FAIL = -1,
  CPKT_SASL_NOMEM = -2,
  CPKT_SASL_BADPARAM = -7,
  CPKT_SASL_NOMECH = -4,
  CPKT_SASL_NOTDONE = -6,
  CPKT_SASL_BADPROT = -5,
  CPKT_SASL_BADAUTH = -13,
  CPKT_SASL_BUFOVER = -3,
  CPKT_SASL_TRYAGAIN = -8,
  CPKT_SASL_BADMAC = -9,
  CPKT_SASL_NOAUTHZ = -14,
  CPKT_SASL_NOUSER = -20,
  CPKT_SASL_BADVERS = -23,
  CPKT_SASL_NOTINIT = -12,
  CPKT_SASL_BADSERV = -10,
  CPKT_SASL_WRONGMECH = -11,
  CPKT_SASL_TOOWEAK = -15,
  CPKT_SASL_ENCRYPT = -16,
  CPKT_SASL_TRANS = -17,
  CPKT_SASL_EXPIRED = -18,
  CPKT_SASL_DISABLED = -19,
  CPKT_SASL_PWLOCK = -21,
  CPKT_SASL_NOCHANGE = -22,
  CPKT_SASL_UNAVAIL = -24,
  CPKT_SASL_NOVERIFY = -26,
  CPKT_SASL_WEAKPASS = -27,
  CPKT_SASL_NOUSERPASS = -28,
  CPKT_SASL_NEED_OLD_PASSWD = -29,
  CPKT_SASL_CONSTRAINT_VIOLAT = -30,
  CPKT_SASL_BADBINDING = -32,
  CPKT_SASL_CONFIGERR = -100,
  CPKT_SASL_NOLOG = 1,
  CPKT_SASL_PATH_PLUGIN = 0,
  CPKT_SASL_PATH_CONFIGURATION = 1,
  CPKT_SASL_VERIFY_PLUGIN_FILE = 0,
  CPKT_SASL_VERIFY_CONFIGURATION_FILE = 1,
  CPKT_SASL_VERIFY_PASSWORD_FILE = 2,
  CPKT_SASL_VERIFY_OTHER_FILE = 3,
  CPKT_SASL_SECURITY_NO_PLAINTEXT = 0x0001,
  CPKT_SASL_SECURITY_NO_ACTIVE = 0x0002,
  CPKT_SASL_SECURITY_NO_DICTIONARY = 0x0004,
  CPKT_SASL_SECURITY_FORWARD_SECRECY = 0x0008,
  CPKT_SASL_SECURITY_NO_ANONYMOUS = 0x0010,
  CPKT_SASL_SECURITY_PASS_CREDENTIALS = 0x0020,
  CPKT_SASL_SECURITY_MUTUAL_AUTH = 0x0040,
  CPKT_SASL_SECURITY_MAXIMUM = 0xFFFF,
  CPKT_SASL_LOG_NONE = 0,
  CPKT_SASL_LOG_ERROR = 1,
  CPKT_SASL_LOG_FAILURE = 2,
  CPKT_SASL_LOG_WARNING = 3,
  CPKT_SASL_LOG_NOTE = 4,
  CPKT_SASL_LOG_DEBUG = 5,
  CPKT_SASL_LOG_TRACE = 6,
  CPKT_SASL_LOG_PASSWORD = 7,
  CPKT_SASL_MAX_MECHANISM_NAME = 20,
  CPKT_SASL_CANONICALIZE_AUTHENTICATION_ID = 0x01,
  CPKT_SASL_CANONICALIZE_NONE = 0,
  CPKT_SASL_CANONICALIZE_AUTHORIZATION_ID = 0x02,
  CPKT_SASL_CANONICALIZE_EXTERNALLY_VERIFIED = 0x04,
  CPKT_SASL_CANONICALIZE_OVERRIDE = 0x08,
  CPKT_SASL_CANONICALIZE_AS_IS_MASK = 0xFFF0,
  CPKT_SASL_CANONICALIZE_VERIFY_HASH = 0x10,
  CPKT_SASL_USE_SUCCESS_DATA = 0x0004,
  CPKT_SASL_NEED_PROXY = 0x0008,
  CPKT_SASL_NEED_HTTP = 0x0010,
  CPKT_SASL_PROPERTY_USERNAME = 0,
  CPKT_SASL_PROPERTY_SSF = 1,
  CPKT_SASL_PROPERTY_MAXIMUM_OUTPUT = 2,
  CPKT_SASL_PROPERTY_DEFAULT_REALM = 3,
  CPKT_SASL_PROPERTY_OPTION_CONTEXT = 4,
  CPKT_SASL_PROPERTY_CALLBACKS = 7,
  CPKT_SASL_PROPERTY_LOCAL_ENDPOINT = 8,
  CPKT_SASL_PROPERTY_REMOTE_ENDPOINT = 9,
  CPKT_SASL_PROPERTY_PLUGIN_ERROR = 10,
  CPKT_SASL_PROPERTY_DELEGATED_CREDENTIALS = 11,
  CPKT_SASL_PROPERTY_SERVICE = 12,
  CPKT_SASL_PROPERTY_SERVER_NAME = 13,
  CPKT_SASL_PROPERTY_AUTHENTICATION_SOURCE = 14,
  CPKT_SASL_PROPERTY_MECHANISM = 15,
  CPKT_SASL_PROPERTY_AUTHENTICATED_USER = 16,
  CPKT_SASL_PROPERTY_APPLICATION = 17,
  CPKT_SASL_PROPERTY_GSS_CREDENTIALS = 18,
  CPKT_SASL_PROPERTY_GSS_PEER_NAME = 19,
  CPKT_SASL_PROPERTY_GSS_LOCAL_NAME = 20,
  CPKT_SASL_PROPERTY_CHANNEL_BINDING = 21,
  CPKT_SASL_PROPERTY_HTTP_REQUEST = 22,
  CPKT_SASL_PROPERTY_EXTERNAL_SSF = 100,
  CPKT_SASL_PROPERTY_SECURITY = 101,
  CPKT_SASL_PROPERTY_EXTERNAL_AUTHENTICATION = 102
};

#define CPKT_SASL_SET_CREATE 1UL
#define CPKT_SASL_SET_DISABLE 2UL
#define CPKT_SASL_SET_NO_PLAINTEXT 4UL
#define CPKT_SASL_SET_CURRENT_MECHANISM 8UL
#define CPKT_SASL_SET_REMOVE CPKT_SASL_SET_CREATE
#define CPKT_SASL_SET_HASH_STRENGTH_BITS(bits) (((bits) / 8UL) << 16)
#define CPKT_SASL_GET_HASH_STRENGTH(flags) ((flags) >> 16)
#define CPKT_SASL_VERSION_MAJOR 2
#define CPKT_SASL_VERSION_MINOR 1
#define CPKT_SASL_VERSION_STEP 28
#define CPKT_SASL_VERSION_FULL                                                 \
  ((CPKT_SASL_VERSION_MAJOR << 16) | (CPKT_SASL_VERSION_MINOR << 8) |          \
   CPKT_SASL_VERSION_STEP)
#define CPKT_SASL_PROPERTY_DEFAULT_CAPACITY 4UL
#define CPKT_SASL_AUX_UID_NUMBER "uidNumber"
#define CPKT_SASL_AUX_GID_NUMBER "gidNumber"
#define CPKT_SASL_AUX_FULL_NAME "gecos"
#define CPKT_SASL_AUX_HOME_DIRECTORY "homeDirectory"
#define CPKT_SASL_AUX_SHELL "loginShell"
#define CPKT_SASL_AUX_MAIL_ADDRESS "mail"
#define CPKT_SASL_AUX_UNIX_MAILBOX "mailMessageStore"
#define CPKT_SASL_AUX_MAIL_CHANNEL "mailSMTPSubmitChannel"
#define CPKT_SASL_AUX_PASSWORD "*userPassword"
#define CPKT_SASL_AUX_PASSWORD_PROPERTY "userPassword"
#define CPKT_SASL_AUX_ALL "*"
#define CPKT_SASL_AUX_END NULL

/* Receiver shell. Returned text is provider-owned until the next matching
 * operation or receiver close, exactly as documented by Cyrus SASL. When
 * start or step returns CPKT_SASL_INTERACT, fill the returned interaction
 * records and pass that same pointer back to the same operation. */
struct cpkt_sasl {
  int (*start)(cpkt_sasl *self, const char *mechanisms,
               cpkt_sasl_interaction **interactions_out,
               const char **output_out, unsigned long *output_byte_count,
               const char **mechanism_out);
  int (*step)(cpkt_sasl *self, const char *input,
              unsigned long input_byte_count,
              cpkt_sasl_interaction **interactions_out, const char **output_out,
              unsigned long *output_byte_count);
  int (*server_start)(cpkt_sasl *self, const char *mechanism, const char *input,
                      unsigned long input_byte_count, const char **output_out,
                      unsigned long *output_byte_count);
  int (*list_mechanisms)(cpkt_sasl *self, const char *user, const char *prefix,
                         const char *separator, const char *suffix,
                         const char **result_out,
                         unsigned long *result_byte_count, int *count_out);
  int (*encode)(cpkt_sasl *self, const char *input,
                unsigned long input_byte_count, const char **output_out,
                unsigned long *output_byte_count);
  int (*decode)(cpkt_sasl *self, const char *input,
                unsigned long input_byte_count, const char **output_out,
                unsigned long *output_byte_count);
  int (*set_external_ssf)(cpkt_sasl *self, unsigned long value);
  int (*set_security_properties)(
      cpkt_sasl *self, const cpkt_sasl_security_properties *properties);
  int (*set_external_authentication)(cpkt_sasl *self, const char *identity);
  const char *(*error_detail)(const cpkt_sasl *self);
  void (*close)(cpkt_sasl *self);
  void *internal;
  int (*encode_vector)(cpkt_sasl *self, const cpkt_sasl_iov *vectors,
                       size_t vector_count, const char **output_out,
                       unsigned long *output_byte_count);
  int (*check_apop)(cpkt_sasl *self, const char *challenge,
                    unsigned long challenge_length, const char *response,
                    unsigned long response_length);
  int (*check_password)(cpkt_sasl *self, const char *user,
                        unsigned long user_length, const char *password,
                        unsigned long password_length);
  int (*user_exists)(cpkt_sasl *self, const char *service, const char *realm,
                     const char *user);
  int (*set_password)(cpkt_sasl *self, const char *user, const char *password,
                      unsigned long password_length, const char *old_password,
                      unsigned long old_password_length, unsigned long flags);
  /** Precompute for this receiver; a null receiver uses global native hooks. */
  int (*idle)(cpkt_sasl *self);
  int (*auxiliary_request)(cpkt_sasl *self, const char *const *names);
  /** Borrow the connection property context. Repeated lookups without a
   * native-context change preserve existing property views. */
  cpkt_sasl_property_context *(*auxiliary_context)(cpkt_sasl *self);
  int (*auxiliary_store)(cpkt_sasl *self, cpkt_sasl_property_context *context,
                         const char *user);
  int (*set_channel_binding)(cpkt_sasl *self,
                             const cpkt_sasl_channel_binding *binding);
  int (*set_http_request)(cpkt_sasl *self,
                          const cpkt_sasl_http_request *request);
  /** Input credential is borrowed until replaced or receiver close. */
  int (*set_gss_credentials)(cpkt_sasl *self,
                             const cpkt_gss_credential *credentials);
  /** Returned credential is the borrowed input supplied to this receiver. */
  int (*get_gss_credentials)(const cpkt_sasl *self,
                             const cpkt_gss_credential **credentials_out);
  /** GSS mechanism delegated handle: read and clear the provider's handle
   * cell on success. Repeated takes return NOTDONE. Release the transferred
   * handle with cpkt_gss_release_credential(). */
  int (*get_delegated_credentials)(const cpkt_sasl *self,
                                   cpkt_gss_credential **credentials_out);
  /** Borrowed mechanism-specific SASL_DELEGATEDCREDS payload. GSS providers
   * return a handle cell; custom mechanisms may return any opaque payload.
   * Valid until the next mechanism step or receiver close. */
  int (*get_delegated_payload)(const cpkt_sasl *self, const void **payload_out);
  /** Returned GSS names are borrowed until the next SASL step or close. */
  int (*get_gss_peer_name)(const cpkt_sasl *self,
                           const cpkt_gss_name **name_out);
  int (*get_gss_local_name)(const cpkt_sasl *self,
                            const cpkt_gss_name **name_out);
  int (*get_text_property)(const cpkt_sasl *self, int property,
                           const char **value_out);
  int (*get_number_property)(const cpkt_sasl *self, int property,
                             unsigned long *value_out);
  /** Set supported text properties: external auth, default realm,
   * application name, and local/remote endpoint. Cyrus copies strings;
   * null clears where its native setter permits it. */
  int (*set_text_property)(cpkt_sasl *self, int property, const char *value);
  /** Caller-owned copy of the security record. The nested property name and
   * value arrays remain borrowed from the last set call until replaced. */
  int (*get_security_properties)(const cpkt_sasl *self,
                                 cpkt_sasl_security_properties *out);
  /** Caller-owned record with borrowed nested strings/entity bytes. Null
   * provider request yields a zero record with SASL_OK. */
  int (*get_http_request)(const cpkt_sasl *self, cpkt_sasl_http_request *out);
  /** Borrowed original receiver callback record selected by SASL_CALLBACK.
   * A null record means no local callback table was supplied. */
  int (*get_callback_record)(const cpkt_sasl *self,
                             const cpkt_sasl_callbacks **out);
  /** Application context for the local option callback, or the global option
   * callback if no local handler exists. NULL when neither is configured;
   * native provider connection pointers are never returned. Cyrus may still
   * use configuration fallback after an application handler declines. */
  int (*get_option_context)(const cpkt_sasl *self, void **out);
  void (*set_error)(cpkt_sasl *self, unsigned long flags, const char *message);
  int (*make_challenge)(cpkt_sasl *self, char *buffer, unsigned long capacity,
                        unsigned long host_flag);
};

/** Returns the implementation identity and version of the bundled SASL
 * provider. */
void cpkt_sasl_version(const char **implementation_out,
                       const char **version_out, int *major_out, int *minor_out,
                       int *step_out, int *patch_out);
/** Deprecated native version-number form; use cpkt_sasl_version() for new
 * consumers. Both returned text pointers are provider-owned. */
void cpkt_sasl_legacy_version(const char **implementation_out,
                              int *version_out);
/** Returns provider-owned text for a SASL status code; do not free it. */
const char *cpkt_sasl_error_string(int status, const char *languages,
                                   const char **language_out);
/** Native idle/precomputation operation. A null receiver requests global
 * work before connection creation. Returns 1 for work done, 0 otherwise. */
int cpkt_sasl_idle(cpkt_sasl *self);
/** Sets a process-wide SASL path before library initialization. */
int cpkt_sasl_set_path(int type, const char *path);
/* Global callbacks provide fallbacks for connection callbacks and receive the
 * current public receiver. Repeated initialization retains the first
 * successful callback record until its matching final finish call. */
/** Initializes client mechanisms and process-wide callbacks. */
int cpkt_sasl_client_initialize(const cpkt_sasl_callbacks *callbacks);
/** Initializes server mechanisms and process-wide callbacks. */
int cpkt_sasl_server_initialize(const cpkt_sasl_callbacks *callbacks,
                                const char *application_name);
/** Releases a client initialization reference after closing client receivers.
 */
int cpkt_sasl_client_finish(void);
/** Releases a server initialization reference after closing server receivers.
 */
int cpkt_sasl_server_finish(void);
/** Releases one initialization reference for each role, as sasl_done does.
 * Plugin and callback storage remains valid while that role has references.
 * Close its receivers before releasing a role's last reference. */
void cpkt_sasl_finish_all(void);
/** Returns a provider-owned NULL-terminated list of globally available
 * mechanisms; valid until provider shutdown or plugin registration. */
const char *const *cpkt_sasl_global_mechanisms(void);
/** Creates an owned client receiver; inspect status_out and close when done. */
cpkt_sasl *cpkt_sasl_client_new(const char *service, const char *server_name,
                                const char *local_endpoint,
                                const char *remote_endpoint,
                                const cpkt_sasl_callbacks *callbacks,
                                unsigned long flags, int *status_out);
/** Creates an owned server receiver; inspect status_out and close when done. */
cpkt_sasl *cpkt_sasl_server_new(const char *service, const char *server_name,
                                const char *user_realm,
                                const char *local_endpoint,
                                const char *remote_endpoint,
                                const cpkt_sasl_callbacks *callbacks,
                                unsigned long flags, int *status_out);
/** Closes a receiver and invalidates its borrowed outputs. NULL is accepted. */
void cpkt_sasl_close(cpkt_sasl *self);

/** Install process-wide allocator/mutex hooks before SASL initialization.
 * The callback functions must remain valid for the process lifetime. */
void cpkt_sasl_set_allocators(cpkt_sasl_malloc_callback malloc_callback,
                              cpkt_sasl_calloc_callback calloc_callback,
                              cpkt_sasl_realloc_callback realloc_callback,
                              cpkt_sasl_free_callback free_callback);
void cpkt_sasl_set_mutexes(cpkt_sasl_mutex_alloc_callback allocate,
                           cpkt_sasl_mutex_lock_callback lock,
                           cpkt_sasl_mutex_unlock_callback unlock,
                           cpkt_sasl_mutex_free_callback release);

/** Property contexts own their request/value storage. Names supplied to
 * request must remain valid until requests are cleared or disposed. */
cpkt_sasl_property_context *cpkt_sasl_property_new(unsigned long estimate);
int cpkt_sasl_property_duplicate(cpkt_sasl_property_context *source,
                                 cpkt_sasl_property_context **copy_out);
int cpkt_sasl_property_request(cpkt_sasl_property_context *context,
                               const char *const *names);
/** Return borrowed native property metadata. Each call observes the current
 * native values, including mutations through another wrapper or plugin. The
 * record array remains stable across unchanged reads; it may be resized when
 * the number of requested names grows. NULL with count zero also reports an
 * empty request list or a metadata allocation failure. */
const cpkt_sasl_property_value *
cpkt_sasl_property_get(cpkt_sasl_property_context *context, size_t *count_out);
/** Fill caller-owned view records for requested names. At least one record per
 * name is required; BUFOVER leaves output untouched. Returned names and values
 * are borrowed until mutation/disposal. A missing value has value_count zero.
 * The nonnegative return counts requested names, including missing values. */
int cpkt_sasl_property_getnames(cpkt_sasl_property_context *context,
                                const char *const *names,
                                cpkt_sasl_property_value *values_out,
                                size_t capacity, size_t *count_out);
int cpkt_sasl_property_format(cpkt_sasl_property_context *context,
                              const char *separator, int separator_length,
                              char *output, unsigned long capacity,
                              unsigned long *length_out);
int cpkt_sasl_property_set(cpkt_sasl_property_context *context,
                           const char *name, const char *value,
                           int value_length);
int cpkt_sasl_property_set_values(cpkt_sasl_property_context *context,
                                  const char *name, const char *const *values);
void cpkt_sasl_property_clear(cpkt_sasl_property_context *context,
                              int clear_requests);
void cpkt_sasl_property_erase(cpkt_sasl_property_context *context,
                              const char *name);
void cpkt_sasl_property_dispose(cpkt_sasl_property_context **context);

/** Stateless helpers write only into caller-owned bounded buffers. */
int cpkt_sasl_base64_encode(const char *input, unsigned long input_length,
                            char *output, unsigned long capacity,
                            unsigned long *length_out);
int cpkt_sasl_base64_decode(const char *input, unsigned long input_length,
                            char *output, unsigned long capacity,
                            unsigned long *length_out);
int cpkt_sasl_utf8_verify(const char *input, unsigned long length);
void cpkt_sasl_erase_buffer(char *buffer, unsigned long length);
char *cpkt_sasl_lowercase(char *text);
int cpkt_sasl_config_initialize(const char *file_name);
void cpkt_sasl_config_finish(void);

/** Random state is owned until cpkt_sasl_random_free(). */
int cpkt_sasl_random_new(cpkt_sasl_random **out);
void cpkt_sasl_random_free(cpkt_sasl_random **random);
int cpkt_sasl_random_seed(cpkt_sasl_random *random, const char *bytes,
                          unsigned long length);
int cpkt_sasl_random_fill(cpkt_sasl_random *random, char *bytes,
                          unsigned long length);
int cpkt_sasl_random_churn(cpkt_sasl_random *random, const char *bytes,
                           unsigned long length);

/** @} */
#endif
