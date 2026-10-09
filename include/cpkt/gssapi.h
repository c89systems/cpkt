#ifndef CPKT_GSSAPI_H
#define CPKT_GSSAPI_H

/**
 * @defgroup cpkt_gssapi GSSAPI C89 facade
 *
 * Opaque names, credentials, contexts, OIDs, and OID sets returned as owned
 * results use their matching release operation. Standard name-type OIDs and
 * OID-set members are borrowed. Output buffers are provider-owned until
 * cpkt_gss_release_buffer(). See docs/gssapi-c89-facade-spec.md.
 * @{
 */

#include <stddef.h>

/* Native null sentinels expressed with facade-owned opaque types. */
#define CPKT_GSS_NO_NAME ((cpkt_gss_name *)0)
#define CPKT_GSS_NO_BUFFER ((cpkt_gss_buffer *)0)
#define CPKT_GSS_NO_OID ((cpkt_gss_oid *)0)
#define CPKT_GSS_NO_OID_SET ((cpkt_gss_oid_set *)0)
#define CPKT_GSS_NO_CONTEXT ((cpkt_gss_context *)0)
#define CPKT_GSS_NO_CREDENTIAL ((cpkt_gss_credential *)0)
#define CPKT_GSS_NO_BUFFER_SET ((cpkt_gss_buffer_set *)0)
#define CPKT_GSS_NO_CREDENTIAL_STORE ((cpkt_gss_store *)0)
#define CPKT_GSS_NO_IOV_BUFFER ((cpkt_gss_iov *)0)
#define CPKT_GSS_NO_CHANNEL_BINDINGS ((cpkt_gss_channel_bindings *)0)
#define CPKT_GSS_EMPTY_BUFFER {0, NULL}
#define CPKT_GSS_BUFFER_IS_EMPTY(buffer)                                       \
  ((buffer) == NULL || ((buffer)->length == 0 && (buffer)->value == NULL))

/** Opaque GSS name; release owned instances with cpkt_gss_release_name(). */
typedef struct cpkt_gss_name cpkt_gss_name;
/** Opaque credential; release an owned instance with
 * cpkt_gss_release_credential(). */
typedef struct cpkt_gss_credential cpkt_gss_credential;
/** Opaque security context; delete an owned instance with
 * cpkt_gss_delete_context(). */
typedef struct cpkt_gss_context cpkt_gss_context;
/** Mechanism or name-type identifier; standard constants are borrowed. */
typedef struct cpkt_gss_oid cpkt_gss_oid;
/** Owned identifier set; release with cpkt_gss_release_oid_set(). */
typedef struct cpkt_gss_oid_set cpkt_gss_oid_set;
/** Provider-owned set of byte buffers. Members are borrowed until release. */
typedef struct cpkt_gss_buffer_set cpkt_gss_buffer_set;
/** Opaque provider-specific name mapping, released with its name and type. */
typedef struct cpkt_gss_any cpkt_gss_any;

/** The bundled GSS allocator family is paired: release allocations made by
 * these helpers with cpkt_gss_allocation_free(). */
void *cpkt_gss_allocation_malloc(size_t size);
/** Zeroed allocation; returns null on allocation failure. */
void *cpkt_gss_allocation_calloc(size_t count, size_t size);
/** Reallocates a GSS allocation, preserving the original on failure. */
void *cpkt_gss_allocation_realloc(void *value, size_t size);
/** Copies a NUL-terminated string into GSS allocator storage. */
char *cpkt_gss_allocation_strdup(const char *value);
/** Releases a GSS allocation; null is accepted. */
void cpkt_gss_allocation_free(void *value);

/* GSSAPI status, QOP, lifetime, and flag values are exact unsigned 32-bit
 * values carried in an unsigned long on every supported target. */
/** GSS major or mechanism-specific minor status carried in 32 bits. */
typedef unsigned long cpkt_gss_status;
/** Quality-of-protection selector carried in 32 bits. */
typedef unsigned long cpkt_gss_qop;
/** Credential or context lifetime in seconds, carried in 32 bits. */
typedef unsigned long cpkt_gss_lifetime;
/** Requested or returned GSS context flags carried in 32 bits. */
typedef unsigned long cpkt_gss_flags;

/** Provider-owned output byte buffer; release with cpkt_gss_release_buffer().
 */
typedef struct cpkt_gss_buffer {
  size_t length;
  void *value;
} cpkt_gss_buffer;

/** Caller-owned endpoint and application binding bytes for context negotiation.
 */
typedef struct cpkt_gss_channel_bindings {
  unsigned long initiator_address_type;
  cpkt_gss_buffer initiator_address;
  unsigned long acceptor_address_type;
  cpkt_gss_buffer acceptor_address;
  cpkt_gss_buffer application_data;
} cpkt_gss_channel_bindings;

/** One GSS IOV entry. Input storage remains caller-owned; entries marked
 * ALLOCATED after an operation are released with cpkt_gss_release_iov(). */
typedef struct cpkt_gss_iov {
  cpkt_gss_flags type;
  cpkt_gss_buffer buffer;
} cpkt_gss_iov;

/** Credential store keys and values are borrowed during a call. */
typedef struct cpkt_gss_store_entry {
  const char *key;
  const char *value;
} cpkt_gss_store_entry;
typedef struct cpkt_gss_store {
  size_t count;
  const cpkt_gss_store_entry *entries;
} cpkt_gss_store;

/** C89 representation of an exact unsigned 64-bit sequence number. */
typedef struct cpkt_gss_u64_words {
  unsigned long high;
  unsigned long low;
} cpkt_gss_u64_words;

/** Borrowed key bytes within an owned lucid context. */
typedef struct cpkt_gss_lucid_key {
  unsigned long type;
  unsigned long length;
  const void *data;
} cpkt_gss_lucid_key;
typedef struct cpkt_gss_lucid_context {
  unsigned long version;
  unsigned long initiate;
  unsigned long endtime;
  cpkt_gss_u64_words send_sequence;
  cpkt_gss_u64_words receive_sequence;
  unsigned long protocol;
  unsigned long sign_algorithm;
  unsigned long seal_algorithm;
  unsigned long have_acceptor_subkey;
  cpkt_gss_lucid_key context_key;
  cpkt_gss_lucid_key acceptor_subkey;
  void *internal;
} cpkt_gss_lucid_context;

/** Narrow Kerberos handles needed by the GSS Kerberos extensions. */
typedef struct cpkt_gss_krb_context cpkt_gss_krb_context;
typedef struct cpkt_gss_krb_cache cpkt_gss_krb_cache;
typedef struct cpkt_gss_krb_keytab cpkt_gss_krb_keytab;
typedef struct cpkt_gss_krb_principal cpkt_gss_krb_principal;
typedef struct cpkt_gss_krb_replay_cache cpkt_gss_krb_replay_cache;
/** Kerberos PAC mapping exported by the GSS mspac name attribute module. */
typedef struct cpkt_gss_pac cpkt_gss_pac;

/** Receives one borrowed, formatted MIT Kerberos trace record at a time.
 * The message is valid only for the duration of the callback. */
typedef void (*cpkt_gss_trace_callback)(void *context, const char *message);

/** Set the process-wide trace sink for subsequently created Kerberos GSS
 * contexts, including SPNEGO/NegoEx. Register before starting worker threads.
 * The context remains caller-owned and must outlive all callbacks. A null
 * callback disables delivery; newly created contexts then retain native
 * KRB5_TRACE file behavior. Existing contexts retain the bridge but stop
 * delivering after it is disabled. */
void cpkt_gss_set_trace_callback(cpkt_gss_trace_callback callback,
                                 void *context);

enum cpkt_gss_constants {
  CPKT_GSS_COMPLETE = 0,
  CPKT_GSS_CREDENTIAL_BOTH = 0,
  CPKT_GSS_CREDENTIAL_INITIATE = 1,
  CPKT_GSS_CREDENTIAL_ACCEPT = 2,
  CPKT_GSS_STATUS_GSS_CODE = 1,
  CPKT_GSS_STATUS_MECHANISM_CODE = 2,
  CPKT_GSS_QOP_DEFAULT = 0,
  CPKT_GSS_FLAG_DELEGATE = 1,
  CPKT_GSS_FLAG_MUTUAL = 2,
  CPKT_GSS_FLAG_REPLAY = 4,
  CPKT_GSS_FLAG_SEQUENCE = 8,
  CPKT_GSS_FLAG_CONFIDENTIALITY = 16,
  CPKT_GSS_FLAG_INTEGRITY = 32,
  CPKT_GSS_FLAG_ANONYMOUS = 64,
  CPKT_GSS_FLAG_PROTECTION_READY = 128,
  CPKT_GSS_FLAG_TRANSFERRABLE = 256
};

#define CPKT_GSS_LIFETIME_INDEFINITE 4294967295UL

/** Exact GSS major-status fields and codes. */
#define CPKT_GSS_CALLING_ERROR(status) ((status) & 0xff000000UL)
#define CPKT_GSS_ROUTINE_ERROR(status) ((status) & 0x00ff0000UL)
#define CPKT_GSS_SUPPLEMENTARY_INFO(status) ((status) & 0x0000ffffUL)
#define CPKT_GSS_CALLING_ERROR_OFFSET 24UL
#define CPKT_GSS_ROUTINE_ERROR_OFFSET 16UL
#define CPKT_GSS_SUPPLEMENTARY_OFFSET 0UL
#define CPKT_GSS_CALLING_ERROR_MASK 255UL
#define CPKT_GSS_ROUTINE_ERROR_MASK 255UL
#define CPKT_GSS_SUPPLEMENTARY_MASK 65535UL
#define CPKT_GSS_CALLING_ERROR_FIELD(status)                                   \
  (((status) >> CPKT_GSS_CALLING_ERROR_OFFSET) & CPKT_GSS_CALLING_ERROR_MASK)
#define CPKT_GSS_ROUTINE_ERROR_FIELD(status)                                   \
  (((status) >> CPKT_GSS_ROUTINE_ERROR_OFFSET) & CPKT_GSS_ROUTINE_ERROR_MASK)
#define CPKT_GSS_SUPPLEMENTARY_INFO_FIELD(status)                              \
  (((status) >> CPKT_GSS_SUPPLEMENTARY_OFFSET) & CPKT_GSS_SUPPLEMENTARY_MASK)
#define CPKT_GSS_STATUS_IS_ERROR(status)                                       \
  (CPKT_GSS_CALLING_ERROR(status) || CPKT_GSS_ROUTINE_ERROR(status))
#define CPKT_GSS_ERROR(status)                                                 \
  (CPKT_GSS_CALLING_ERROR(status) | CPKT_GSS_ROUTINE_ERROR(status))
#define CPKT_GSS_S_CALL_INACCESSIBLE_READ 0x01000000UL
#define CPKT_GSS_S_CALL_INACCESSIBLE_WRITE 0x02000000UL
#define CPKT_GSS_S_CALL_BAD_STRUCTURE 0x03000000UL
#define CPKT_GSS_S_BAD_MECHANISM 0x00010000UL
#define CPKT_GSS_S_BAD_NAME 0x00020000UL
#define CPKT_GSS_S_BAD_NAME_TYPE 0x00030000UL
#define CPKT_GSS_S_BAD_BINDINGS 0x00040000UL
#define CPKT_GSS_S_BAD_STATUS 0x00050000UL
#define CPKT_GSS_S_BAD_MIC 0x00060000UL
#define CPKT_GSS_S_BAD_SIGNATURE CPKT_GSS_S_BAD_MIC
#define CPKT_GSS_S_NO_CREDENTIAL 0x00070000UL
#define CPKT_GSS_S_NO_CONTEXT 0x00080000UL
#define CPKT_GSS_S_DEFECTIVE_TOKEN 0x00090000UL
#define CPKT_GSS_S_DEFECTIVE_CREDENTIAL 0x000a0000UL
#define CPKT_GSS_S_CREDENTIALS_EXPIRED 0x000b0000UL
#define CPKT_GSS_S_CONTEXT_EXPIRED 0x000c0000UL
#define CPKT_GSS_S_FAILURE 0x000d0000UL
#define CPKT_GSS_S_CREDENTIAL_UNAVAILABLE CPKT_GSS_S_FAILURE
#define CPKT_GSS_S_BAD_QOP 0x000e0000UL
#define CPKT_GSS_S_UNAUTHORIZED 0x000f0000UL
#define CPKT_GSS_S_UNAVAILABLE 0x00100000UL
#define CPKT_GSS_S_DUPLICATE_ELEMENT 0x00110000UL
#define CPKT_GSS_S_NAME_NOT_MN 0x00120000UL
#define CPKT_GSS_S_BAD_MECHANISM_ATTRIBUTE 0x00130000UL
#define CPKT_GSS_S_CONTINUE_NEEDED 1UL
#define CPKT_GSS_S_DUPLICATE_TOKEN 2UL
#define CPKT_GSS_S_OLD_TOKEN 4UL
#define CPKT_GSS_S_UNSEQUENCED_TOKEN 8UL
#define CPKT_GSS_S_GAP_TOKEN 16UL
#define CPKT_GSS_PRF_KEY_FULL 0UL
#define CPKT_GSS_PRF_KEY_PARTIAL 1UL
#define CPKT_GSS_FLAG_DELEGATE_POLICY 32768UL
#define CPKT_GSS_FLAG_CHANNEL_BOUND 2048UL
#define CPKT_GSS_FLAG_DCE_STYLE 4096UL
#define CPKT_GSS_FLAG_IDENTIFY 8192UL
#define CPKT_GSS_FLAG_EXTENDED_ERROR 16384UL

/** Channel-binding address families; the caller supplies their native byte
 * encoding in cpkt_gss_channel_bindings. */
#define CPKT_GSS_AF_UNSPEC 0UL
#define CPKT_GSS_AF_LOCAL 1UL
#define CPKT_GSS_AF_INET 2UL
#define CPKT_GSS_AF_IMPLINK 3UL
#define CPKT_GSS_AF_PUP 4UL
#define CPKT_GSS_AF_CHAOS 5UL
#define CPKT_GSS_AF_NS 6UL
#define CPKT_GSS_AF_NBS 7UL
#define CPKT_GSS_AF_ECMA 8UL
#define CPKT_GSS_AF_DATAKIT 9UL
#define CPKT_GSS_AF_CCITT 10UL
#define CPKT_GSS_AF_SNA 11UL
#define CPKT_GSS_AF_DECNET 12UL
#define CPKT_GSS_AF_DLI 13UL
#define CPKT_GSS_AF_LAT 14UL
#define CPKT_GSS_AF_HYLINK 15UL
#define CPKT_GSS_AF_APPLETALK 16UL
#define CPKT_GSS_AF_BSC 17UL
#define CPKT_GSS_AF_DSS 18UL
#define CPKT_GSS_AF_OSI 19UL
#define CPKT_GSS_AF_NETBIOS 20UL
#define CPKT_GSS_AF_X25 21UL
#define CPKT_GSS_AF_NULL_ADDRESS 255UL

#define CPKT_GSS_IOV_EMPTY 0UL
#define CPKT_GSS_IOV_DATA 1UL
#define CPKT_GSS_IOV_HEADER 2UL
#define CPKT_GSS_IOV_MECH_PARAMS 3UL
#define CPKT_GSS_IOV_TRAILER 7UL
#define CPKT_GSS_IOV_PADDING 9UL
#define CPKT_GSS_IOV_STREAM 10UL
#define CPKT_GSS_IOV_SIGN_ONLY 11UL
#define CPKT_GSS_IOV_MIC_TOKEN 12UL
#define CPKT_GSS_IOV_ALLOCATE 65536UL
#define CPKT_GSS_IOV_ALLOCATED 131072UL
#define CPKT_GSS_IOV_FLAG_MASK 0xffff0000UL
#define CPKT_GSS_IOV_TYPE(type) ((type) & ~CPKT_GSS_IOV_FLAG_MASK)
#define CPKT_GSS_IOV_FLAGS(type) ((type) & CPKT_GSS_IOV_FLAG_MASK)

/** Returns nonzero when the major status contains a GSSAPI error. */
int cpkt_gss_status_is_error(cpkt_gss_status status);
/** Borrowed OID for a user-name string. */
const cpkt_gss_oid *cpkt_gss_name_type_user(void);
/** Borrowed OID for a machine-specific numeric user ID. */
const cpkt_gss_oid *cpkt_gss_name_type_machine_uid(void);
/** Borrowed OID for a textual user ID. */
const cpkt_gss_oid *cpkt_gss_name_type_string_uid(void);
/** Borrowed OID for a host-based service name. */
const cpkt_gss_oid *cpkt_gss_name_type_hostbased_service(void);
/** Borrowed OID for an anonymous name. */
const cpkt_gss_oid *cpkt_gss_name_type_anonymous(void);
/** Borrowed OID for an exported binary name. */
const cpkt_gss_oid *cpkt_gss_name_type_exported_name(void);
/** Borrowed descriptor bytes; valid while the OID or its set is alive. */
int cpkt_gss_oid_view(const cpkt_gss_oid *oid, const void **bytes_out,
                      size_t *length_out);

/** Standard and Kerberos extension OIDs. Returned descriptors are borrowed
 * for the provider lifetime. */
enum cpkt_gss_known_oid {
  CPKT_GSS_OID_HOSTBASED_SERVICE_X,
  CPKT_GSS_OID_COMPOSITE_EXPORT_NAME,
  CPKT_GSS_OID_INQ_SSPI_SESSION_KEY,
  CPKT_GSS_OID_INQ_ODBC_SESSION_KEY,
  CPKT_GSS_OID_INQ_NEGOEX_KEY,
  CPKT_GSS_OID_INQ_NEGOEX_VERIFY_KEY,
  CPKT_GSS_OID_SEC_CONTEXT_SASL_SSF,
  CPKT_GSS_OID_MA_MECH_CONCRETE,
  CPKT_GSS_OID_MA_MECH_PSEUDO,
  CPKT_GSS_OID_MA_MECH_COMPOSITE,
  CPKT_GSS_OID_MA_MECH_NEGO,
  CPKT_GSS_OID_MA_MECH_GLUE,
  CPKT_GSS_OID_MA_NOT_MECH,
  CPKT_GSS_OID_MA_DEPRECATED,
  CPKT_GSS_OID_MA_NOT_DEFAULT_MECH,
  CPKT_GSS_OID_MA_INITIAL_TOKEN_FRAMED,
  CPKT_GSS_OID_MA_AUTH_INIT,
  CPKT_GSS_OID_MA_AUTH_TARGET,
  CPKT_GSS_OID_MA_AUTH_INIT_INIT,
  CPKT_GSS_OID_MA_AUTH_TARGET_INIT,
  CPKT_GSS_OID_MA_AUTH_INIT_ANON,
  CPKT_GSS_OID_MA_AUTH_TARGET_ANON,
  CPKT_GSS_OID_MA_DELEGATE_CRED,
  CPKT_GSS_OID_MA_INTEGRITY,
  CPKT_GSS_OID_MA_CONFIDENTIALITY,
  CPKT_GSS_OID_MA_MIC,
  CPKT_GSS_OID_MA_WRAP,
  CPKT_GSS_OID_MA_PROTECTION_READY,
  CPKT_GSS_OID_MA_REPLAY_DETECTION,
  CPKT_GSS_OID_MA_SEQUENCE_DETECTION,
  CPKT_GSS_OID_MA_CHANNEL_BINDINGS,
  CPKT_GSS_OID_MA_FORWARD_SECRECY,
  CPKT_GSS_OID_MA_COMPRESSION,
  CPKT_GSS_OID_MA_CONTEXT_TRANSFER,
  CPKT_GSS_OID_MA_NEGOEX_AND_SPNEGO,
  CPKT_GSS_OID_KRB5_MECH_OLD,
  CPKT_GSS_OID_KRB5_MECH_WRONG,
  CPKT_GSS_OID_IAKERB_MECH,
  CPKT_GSS_OID_KRB5_ENTERPRISE_NAME,
  CPKT_GSS_OID_KRB5_X509_CERT,
  CPKT_GSS_OID_KRB5_NAME,
  CPKT_GSS_OID_KRB5_PRINCIPAL,
  CPKT_GSS_OID_KRB5_CRED_NO_CI_FLAGS,
  CPKT_GSS_OID_KRB5_GET_CRED_IMPERSONATOR
};
/** Returns the borrowed standard or Kerberos OID selected by which. */
const cpkt_gss_oid *cpkt_gss_known_oid(int which);
/** Returns a borrowed Kerberos mechanism set for the provider lifetime. */
const cpkt_gss_oid_set *cpkt_gss_krb5_mechanism_set(int which);
/** Borrowed static attribute bytes, valid for the provider lifetime. Do not
 * release or modify the returned view. */
int cpkt_gss_local_login_attribute(const void **bytes_out, size_t *length_out);

/** Releases provider-owned bytes and clears the buffer on success. */
cpkt_gss_status cpkt_gss_release_buffer(cpkt_gss_status *minor_status_out,
                                        cpkt_gss_buffer *buffer);
/** Releases a dynamically owned OID through the GSS provider. */
cpkt_gss_status cpkt_gss_release_oid(cpkt_gss_status *minor_status_out,
                                     cpkt_gss_oid **oid_in_out);
/** Releases an owned OID set; its member OIDs then become invalid. */
cpkt_gss_status cpkt_gss_release_oid_set(cpkt_gss_status *minor_status_out,
                                         cpkt_gss_oid_set **set_in_out);
/** Releases an owned name and clears its handle. */
cpkt_gss_status cpkt_gss_release_name(cpkt_gss_status *minor_status_out,
                                      cpkt_gss_name **name_in_out);
/** Releases an owned credential and clears its handle. */
cpkt_gss_status
cpkt_gss_release_credential(cpkt_gss_status *minor_status_out,
                            cpkt_gss_credential **credential_in_out);
/** Deletes a security context and may produce a final output token. */
cpkt_gss_status cpkt_gss_delete_context(cpkt_gss_status *minor_status_out,
                                        cpkt_gss_context **context_in_out,
                                        cpkt_gss_buffer *output_token);

/** Returns an owned set of mechanisms supported by the provider.
 * set_out is required; NULL returns CALL_BAD_STRUCTURE and minor EINVAL. */
cpkt_gss_status cpkt_gss_indicate_mechanisms(cpkt_gss_status *minor_status_out,
                                             cpkt_gss_oid_set **set_out);
/** Returns the number of entries in a borrowed or owned OID set. */
size_t cpkt_gss_oid_set_count(const cpkt_gss_oid_set *set);
/** Returns a borrowed OID member; keep the set alive while using it. */
const cpkt_gss_oid *cpkt_gss_oid_set_at(const cpkt_gss_oid_set *set,
                                        size_t index);
/** Creates an empty, owned OID set. set_out is required; NULL returns
 * CALL_BAD_STRUCTURE and minor EINVAL before provider allocation. */
cpkt_gss_status cpkt_gss_create_oid_set(cpkt_gss_status *minor_status_out,
                                        cpkt_gss_oid_set **set_out);
/** Adds an OID to a mutable set, copying its identifier bytes.
 * set_in_out is required; NULL returns CALL_BAD_STRUCTURE and minor EINVAL. */
cpkt_gss_status cpkt_gss_add_oid_to_set(cpkt_gss_status *minor_status_out,
                                        const cpkt_gss_oid *oid,
                                        cpkt_gss_oid_set **set_in_out);
/** Reports whether the given OID is present in a set. */
cpkt_gss_status cpkt_gss_oid_set_contains(cpkt_gss_status *minor_status_out,
                                          const cpkt_gss_oid *oid,
                                          const cpkt_gss_oid_set *set,
                                          int *present_out);
/** Parses dotted-decimal OID text into an owned identifier. oid_out is
 * required; NULL returns CALL_BAD_STRUCTURE and minor EINVAL. */
cpkt_gss_status cpkt_gss_oid_from_text(cpkt_gss_status *minor_status_out,
                                       const cpkt_gss_buffer *text,
                                       cpkt_gss_oid **oid_out);
/** Copies DER OID bytes into an owned descriptor. Release with
 * cpkt_gss_release_oid(). Rejects null bytes for nonempty input. */
cpkt_gss_status cpkt_gss_oid_from_bytes(cpkt_gss_status *minor_status_out,
                                        const void *bytes, size_t length,
                                        cpkt_gss_oid **oid_out);
/** Formats an OID into a provider-owned output buffer. */
cpkt_gss_status cpkt_gss_oid_to_text(cpkt_gss_status *minor_status_out,
                                     const cpkt_gss_oid *oid,
                                     cpkt_gss_buffer *text_out);

/** Imports external name bytes as an owned GSS name. name_out is required;
 * NULL returns CALL_BAD_STRUCTURE and minor EINVAL. */
cpkt_gss_status cpkt_gss_import_name(cpkt_gss_status *minor_status_out,
                                     const cpkt_gss_buffer *input,
                                     const cpkt_gss_oid *name_type,
                                     cpkt_gss_name **name_out);
/** Formats a name into a provider-owned output buffer. */
cpkt_gss_status cpkt_gss_display_name(cpkt_gss_status *minor_status_out,
                                      const cpkt_gss_name *name,
                                      cpkt_gss_buffer *text_out,
                                      const cpkt_gss_oid **name_type_out);
/** Reports whether two GSS names identify the same principal. */
cpkt_gss_status cpkt_gss_compare_names(cpkt_gss_status *minor_status_out,
                                       const cpkt_gss_name *left,
                                       const cpkt_gss_name *right,
                                       int *equal_out);
/** Creates an independent owned copy of a GSS name. copy_out is required;
 * NULL returns CALL_BAD_STRUCTURE and minor EINVAL. */
cpkt_gss_status cpkt_gss_duplicate_name(cpkt_gss_status *minor_status_out,
                                        const cpkt_gss_name *source,
                                        cpkt_gss_name **copy_out);
/** Converts a name to a mechanism-specific owned name. canonical_out is
 * required; NULL returns CALL_BAD_STRUCTURE and minor EINVAL. */
cpkt_gss_status cpkt_gss_canonicalize_name(cpkt_gss_status *minor_status_out,
                                           const cpkt_gss_name *source,
                                           const cpkt_gss_oid *mechanism,
                                           cpkt_gss_name **canonical_out);
/** Serializes a mechanism name into a provider-owned output buffer. */
cpkt_gss_status cpkt_gss_export_name(cpkt_gss_status *minor_status_out,
                                     const cpkt_gss_name *name,
                                     cpkt_gss_buffer *token_out);

/** Obtains an owned credential for the requested usage and mechanisms. */
cpkt_gss_status cpkt_gss_acquire_credential(
    cpkt_gss_status *minor_status_out, const cpkt_gss_name *desired_name,
    cpkt_gss_lifetime requested_lifetime,
    const cpkt_gss_oid_set *desired_mechanisms, int usage,
    cpkt_gss_credential **credential_out,
    cpkt_gss_oid_set **actual_mechanisms_out, cpkt_gss_lifetime *lifetime_out);
/** Reads a credential's name, lifetime, usage, and mechanism set. */
cpkt_gss_status cpkt_gss_inquire_credential(
    cpkt_gss_status *minor_status_out, const cpkt_gss_credential *credential,
    cpkt_gss_name **name_out, cpkt_gss_lifetime *lifetime_out, int *usage_out,
    cpkt_gss_oid_set **mechanisms_out);

/** Advances initiator-side context negotiation; exchange output tokens with the
 * peer. context_in_out is required; NULL returns CALL_BAD_STRUCTURE and
 * minor EINVAL before provider allocation. */
cpkt_gss_status cpkt_gss_init_context(
    cpkt_gss_status *minor_status_out, const cpkt_gss_credential *credential,
    cpkt_gss_context **context_in_out, const cpkt_gss_name *target,
    const cpkt_gss_oid *mechanism, cpkt_gss_flags requested_flags,
    cpkt_gss_lifetime requested_lifetime,
    const cpkt_gss_channel_bindings *channel_bindings,
    const cpkt_gss_buffer *input_token,
    const cpkt_gss_oid **actual_mechanism_out, cpkt_gss_buffer *output_token,
    cpkt_gss_flags *returned_flags_out, cpkt_gss_lifetime *lifetime_out);
/** Advances acceptor-side context negotiation; exchange output tokens with the
 * peer. context_in_out is required; NULL returns CALL_BAD_STRUCTURE and
 * minor EINVAL before provider allocation. */
cpkt_gss_status cpkt_gss_accept_context(
    cpkt_gss_status *minor_status_out, cpkt_gss_context **context_in_out,
    const cpkt_gss_credential *credential, const cpkt_gss_buffer *input_token,
    const cpkt_gss_channel_bindings *channel_bindings,
    cpkt_gss_name **source_out, const cpkt_gss_oid **mechanism_out,
    cpkt_gss_buffer *output_token, cpkt_gss_flags *returned_flags_out,
    cpkt_gss_lifetime *lifetime_out,
    cpkt_gss_credential **delegated_credential_out);
/** Returns the remaining lifetime of an established context. */
cpkt_gss_status cpkt_gss_context_lifetime(cpkt_gss_status *minor_status_out,
                                          const cpkt_gss_context *context,
                                          cpkt_gss_lifetime *lifetime_out);

/** Computes a message integrity token in a provider-owned buffer. */
cpkt_gss_status cpkt_gss_get_mic(cpkt_gss_status *minor_status_out,
                                 const cpkt_gss_context *context,
                                 cpkt_gss_qop qop,
                                 const cpkt_gss_buffer *message,
                                 cpkt_gss_buffer *token_out);
/** Verifies a message integrity token against the supplied message. */
cpkt_gss_status cpkt_gss_verify_mic(cpkt_gss_status *minor_status_out,
                                    const cpkt_gss_context *context,
                                    const cpkt_gss_buffer *message,
                                    const cpkt_gss_buffer *token,
                                    cpkt_gss_qop *qop_out);
/** Protects a message and returns provider-owned output bytes. */
cpkt_gss_status cpkt_gss_wrap(cpkt_gss_status *minor_status_out,
                              const cpkt_gss_context *context,
                              int confidentiality_requested, cpkt_gss_qop qop,
                              const cpkt_gss_buffer *input,
                              int *confidentiality_out,
                              cpkt_gss_buffer *output_out);
/** Removes message protection and returns provider-owned plaintext. */
cpkt_gss_status cpkt_gss_unwrap(cpkt_gss_status *minor_status_out,
                                const cpkt_gss_context *context,
                                const cpkt_gss_buffer *input,
                                cpkt_gss_buffer *output_out,
                                int *confidentiality_out,
                                cpkt_gss_qop *qop_out);
/** Formats one status fragment; continue while message_context is nonzero. */
cpkt_gss_status cpkt_gss_display_status(cpkt_gss_status *minor_status_out,
                                        cpkt_gss_status status, int status_type,
                                        const cpkt_gss_oid *mechanism,
                                        cpkt_gss_status *message_context_in_out,
                                        cpkt_gss_buffer *text_out);

/** Releases an owned set, including every member. NULL is accepted. */
cpkt_gss_status cpkt_gss_release_buffer_set(cpkt_gss_status *minor_out,
                                            cpkt_gss_buffer_set **set);
/** Creates an owned empty buffer set; release with
 * cpkt_gss_release_buffer_set(). */
cpkt_gss_status cpkt_gss_create_buffer_set(cpkt_gss_status *minor_out,
                                           cpkt_gss_buffer_set **set_out);
/** Copies the supplied member bytes into the owned buffer set. */
cpkt_gss_status cpkt_gss_add_buffer_set_member(cpkt_gss_status *minor_out,
                                               const cpkt_gss_buffer *member,
                                               cpkt_gss_buffer_set **set);
/** Returns the number of buffers in the set. */
size_t cpkt_gss_buffer_set_count(const cpkt_gss_buffer_set *set);
/** Returns a borrowed member view; false for an out-of-range index. */
int cpkt_gss_buffer_set_at(const cpkt_gss_buffer_set *set, size_t index,
                           cpkt_gss_buffer *member_out);

/** IOV calls preserve per-entry allocation flags and byte ownership. */
cpkt_gss_status cpkt_gss_wrap_iov(cpkt_gss_status *minor_out,
                                  const cpkt_gss_context *context, int conf,
                                  cpkt_gss_qop qop, int *conf_out,
                                  cpkt_gss_iov *iov, size_t count);
/** Verifies and unwraps IOV entries, preserving their allocation flags. */
cpkt_gss_status cpkt_gss_unwrap_iov(cpkt_gss_status *minor_out,
                                    const cpkt_gss_context *context,
                                    int *conf_out, cpkt_gss_qop *qop_out,
                                    cpkt_gss_iov *iov, size_t count);
/** Calculates the IOV buffer lengths required for wrapping a message. */
cpkt_gss_status cpkt_gss_wrap_iov_length(cpkt_gss_status *minor_out,
                                         const cpkt_gss_context *context,
                                         int conf, cpkt_gss_qop qop,
                                         int *conf_out, cpkt_gss_iov *iov,
                                         size_t count);
/** Generates a MIC for the supplied IOV entries. */
cpkt_gss_status cpkt_gss_get_mic_iov(cpkt_gss_status *minor_out,
                                     const cpkt_gss_context *context,
                                     cpkt_gss_qop qop, cpkt_gss_iov *iov,
                                     size_t count);
/** Calculates the MIC buffer length for the supplied IOV entries. */
cpkt_gss_status cpkt_gss_get_mic_iov_length(cpkt_gss_status *minor_out,
                                            const cpkt_gss_context *context,
                                            cpkt_gss_qop qop, cpkt_gss_iov *iov,
                                            size_t count);
/** Verifies the MIC for the supplied IOV entries and reports the QOP. */
cpkt_gss_status cpkt_gss_verify_mic_iov(cpkt_gss_status *minor_out,
                                        const cpkt_gss_context *context,
                                        cpkt_gss_qop *qop_out,
                                        cpkt_gss_iov *iov, size_t count);
/** Releases provider-allocated IOV bytes according to each entry's flags. */
cpkt_gss_status cpkt_gss_release_iov(cpkt_gss_status *minor_out,
                                     cpkt_gss_iov *iov, size_t count);

/** Returns an owned OID set of mechanisms applicable to a name. */
cpkt_gss_status
cpkt_gss_inquire_mechanisms_for_name(cpkt_gss_status *minor_out,
                                     const cpkt_gss_name *name,
                                     cpkt_gss_oid_set **mechanisms_out);
/** Returns an owned set of name types supported by the mechanism. */
cpkt_gss_status
cpkt_gss_inquire_names_for_mechanism(cpkt_gss_status *minor_out,
                                     const cpkt_gss_oid *mechanism,
                                     cpkt_gss_oid_set **name_types_out);
/** Formats the name using display_type into an owned output buffer. */
cpkt_gss_status cpkt_gss_display_name_ext(cpkt_gss_status *minor_out,
                                          const cpkt_gss_name *name,
                                          const cpkt_gss_oid *display_type,
                                          cpkt_gss_buffer *output);
/** Exports the name and its attributes into an owned token buffer. */
cpkt_gss_status cpkt_gss_export_name_composite(cpkt_gss_status *minor_out,
                                               const cpkt_gss_name *name,
                                               cpkt_gss_buffer *output);
/** Reports the name's mechanism and returns an owned attribute buffer set. */
cpkt_gss_status cpkt_gss_inquire_name(cpkt_gss_status *minor_out,
                                      const cpkt_gss_name *name,
                                      int *is_mechanism_name_out,
                                      const cpkt_gss_oid **mechanism_out,
                                      cpkt_gss_buffer_set **attributes_out);
/** Returns owned attribute buffers; more_in_out controls iteration. */
cpkt_gss_status cpkt_gss_get_name_attribute(
    cpkt_gss_status *minor_out, const cpkt_gss_name *name,
    const cpkt_gss_buffer *attribute, int *authenticated_out, int *complete_out,
    cpkt_gss_buffer *value_out, cpkt_gss_buffer *display_value_out,
    int *more_in_out);
/** Sets the named attribute using the supplied value and completeness flag. */
cpkt_gss_status cpkt_gss_set_name_attribute(cpkt_gss_status *minor_out,
                                            const cpkt_gss_name *name,
                                            int complete,
                                            const cpkt_gss_buffer *attribute,
                                            const cpkt_gss_buffer *value);
/** Deletes the named attribute from the supplied name. */
cpkt_gss_status
cpkt_gss_delete_name_attribute(cpkt_gss_status *minor_out,
                               const cpkt_gss_name *name,
                               const cpkt_gss_buffer *attribute);
/** Maps a GSS name to a local name in an owned output buffer. */
cpkt_gss_status cpkt_gss_local_name(cpkt_gss_status *minor_out,
                                    const cpkt_gss_name *name,
                                    const cpkt_gss_oid *mechanism,
                                    cpkt_gss_buffer *local_name_out);
/** Checks whether the GSS name is authorized for the local user name. */
cpkt_gss_status cpkt_gss_authorize_local_name(cpkt_gss_status *minor_out,
                                              const cpkt_gss_name *name,
                                              const cpkt_gss_name *local_user);
/** Returns whether the GSS name is authorized for the supplied user. */
int cpkt_gss_user_ok(const cpkt_gss_name *name, const char *user);

/** Context export consumes its input handle. Output tokens are released with
 * cpkt_gss_release_buffer(); imported contexts need delete_context(). */
cpkt_gss_status cpkt_gss_inquire_context(
    cpkt_gss_status *minor_out, const cpkt_gss_context *context,
    cpkt_gss_name **initiator_out, cpkt_gss_name **acceptor_out,
    cpkt_gss_lifetime *lifetime_out, const cpkt_gss_oid **mechanism_out,
    cpkt_gss_flags *flags_out, int *locally_initiated_out, int *open_out);
/** Consumes the context on successful export and returns an owned token. */
cpkt_gss_status cpkt_gss_export_context(cpkt_gss_status *minor_out,
                                        cpkt_gss_context **context,
                                        cpkt_gss_buffer *token_out);
/** Imports a context token into an owned handle; delete it when finished. */
cpkt_gss_status cpkt_gss_import_context(cpkt_gss_status *minor_out,
                                        const cpkt_gss_buffer *token,
                                        cpkt_gss_context **context_out);
/** Processes a peer context-management token with the provider. */
cpkt_gss_status cpkt_gss_process_context_token(cpkt_gss_status *minor_out,
                                               const cpkt_gss_context *context,
                                               const cpkt_gss_buffer *token);
/** Completes provider-specific processing of an authentication token. */
cpkt_gss_status cpkt_gss_complete_auth_token(cpkt_gss_status *minor_out,
                                             const cpkt_gss_context *context,
                                             const cpkt_gss_buffer *token);
/** Calculates the maximum input size for the requested wrapped output size. */
cpkt_gss_status cpkt_gss_wrap_size_limit(cpkt_gss_status *minor_out,
                                         const cpkt_gss_context *context,
                                         int conf, cpkt_gss_qop qop,
                                         unsigned long output_size,
                                         unsigned long *maximum_input_out);
/** Derives output_byte_count bytes into an owned GSS buffer. */
cpkt_gss_status cpkt_gss_pseudo_random(cpkt_gss_status *minor_out,
                                       const cpkt_gss_context *context,
                                       int key_selector,
                                       const cpkt_gss_buffer *input,
                                       long output_byte_count,
                                       cpkt_gss_buffer *output);

/** Credential constructors return owned handles. Mechanism sets returned by
 * these calls are independently owned and need release_oid_set(). */
cpkt_gss_status cpkt_gss_acquire_credential_from(
    cpkt_gss_status *minor_out, const cpkt_gss_name *name,
    cpkt_gss_lifetime lifetime, const cpkt_gss_oid_set *mechanisms, int usage,
    const cpkt_gss_store *store, cpkt_gss_credential **credential_out,
    cpkt_gss_oid_set **actual_mechanisms_out,
    cpkt_gss_lifetime *actual_lifetime_out);
/** Acquires an owned credential using the borrowed password bytes. */
cpkt_gss_status cpkt_gss_acquire_credential_with_password(
    cpkt_gss_status *minor_out, const cpkt_gss_name *name,
    const cpkt_gss_buffer *password, cpkt_gss_lifetime lifetime,
    const cpkt_gss_oid_set *mechanisms, int usage,
    cpkt_gss_credential **credential_out,
    cpkt_gss_oid_set **actual_mechanisms_out,
    cpkt_gss_lifetime *actual_lifetime_out);
/** Acquires an owned credential for name using the impersonator credential. */
cpkt_gss_status cpkt_gss_acquire_credential_impersonating(
    cpkt_gss_status *minor_out, const cpkt_gss_credential *impersonator,
    const cpkt_gss_name *name, cpkt_gss_lifetime lifetime,
    const cpkt_gss_oid_set *mechanisms, int usage,
    cpkt_gss_credential **credential_out,
    cpkt_gss_oid_set **actual_mechanisms_out,
    cpkt_gss_lifetime *actual_lifetime_out);

/** Adds a mechanism credential and returns owned credential and set outputs. */
cpkt_gss_status cpkt_gss_add_credential(
    cpkt_gss_status *minor_out, const cpkt_gss_credential *input,
    const cpkt_gss_name *name, const cpkt_gss_oid *mechanism, int usage,
    cpkt_gss_lifetime initiator_lifetime, cpkt_gss_lifetime acceptor_lifetime,
    const cpkt_gss_store *store, const cpkt_gss_buffer *password,
    const cpkt_gss_credential *impersonator,
    cpkt_gss_credential **credential_out,
    cpkt_gss_oid_set **actual_mechanisms_out,
    cpkt_gss_lifetime *initiator_lifetime_out,
    cpkt_gss_lifetime *acceptor_lifetime_out);
/** Reports mechanism-specific credential usage, lifetimes and owned name. */
cpkt_gss_status cpkt_gss_inquire_credential_by_mechanism(
    cpkt_gss_status *minor_out, const cpkt_gss_credential *credential,
    const cpkt_gss_oid *mechanism, cpkt_gss_name **name_out,
    cpkt_gss_lifetime *initiator_lifetime_out,
    cpkt_gss_lifetime *acceptor_lifetime_out, int *usage_out);
/** Returns an owned buffer set for the mechanism-specific credential query. */
cpkt_gss_status cpkt_gss_inquire_credential_by_oid(
    cpkt_gss_status *minor_out, const cpkt_gss_credential *credential,
    const cpkt_gss_oid *desired_object, cpkt_gss_buffer_set **data_out);
/** Sets a provider-specific credential option using borrowed value bytes. */
cpkt_gss_status cpkt_gss_set_credential_option(cpkt_gss_status *minor_out,
                                               cpkt_gss_credential **credential,
                                               const cpkt_gss_oid *option,
                                               const cpkt_gss_buffer *value);
/** Restricts the credential's negotiation mechanisms to the supplied set. */
cpkt_gss_status
cpkt_gss_set_negotiation_mechanisms(cpkt_gss_status *minor_out,
                                    cpkt_gss_credential *credential,
                                    const cpkt_gss_oid_set *mechanisms);
/** Stores a credential and returns an owned set of stored mechanisms. */
cpkt_gss_status cpkt_gss_store_credential(
    cpkt_gss_status *minor_out, const cpkt_gss_credential *credential,
    int usage, const cpkt_gss_oid *mechanism, int overwrite,
    int default_credential, const cpkt_gss_store *store,
    cpkt_gss_oid_set **stored_mechanisms_out, int *stored_usage_out);

/** Mechanism attributes and names return owned sets or buffers; returned
 * mechanism OIDs are borrowed from the provider. */
cpkt_gss_status cpkt_gss_indicate_mechanisms_by_attributes(
    cpkt_gss_status *minor_out, const cpkt_gss_oid_set *desired,
    const cpkt_gss_oid_set *excluded, const cpkt_gss_oid_set *critical,
    cpkt_gss_oid_set **mechanisms_out);
/** Returns owned sets of supported and known mechanism attributes. */
cpkt_gss_status cpkt_gss_inquire_mechanism_attributes(
    cpkt_gss_status *minor_out, const cpkt_gss_oid *mechanism,
    cpkt_gss_oid_set **supported_out, cpkt_gss_oid_set **known_out);
/** Returns owned name and description buffers for a mechanism attribute. */
cpkt_gss_status cpkt_gss_display_mechanism_attribute(
    cpkt_gss_status *minor_out, const cpkt_gss_oid *attribute,
    cpkt_gss_buffer *name_out, cpkt_gss_buffer *short_description_out,
    cpkt_gss_buffer *long_description_out);
/** Returns the borrowed mechanism OID corresponding to the SASL name. */
cpkt_gss_status
cpkt_gss_mechanism_for_sasl_name(cpkt_gss_status *minor_out,
                                 const cpkt_gss_buffer *sasl_name,
                                 const cpkt_gss_oid **mechanism_out);
/** Returns owned SASL name, mechanism name and description buffers. */
cpkt_gss_status cpkt_gss_sasl_name_for_mechanism(
    cpkt_gss_status *minor_out, const cpkt_gss_oid *mechanism,
    cpkt_gss_buffer *sasl_name_out, cpkt_gss_buffer *mechanism_name_out,
    cpkt_gss_buffer *description_out);
/** Returns whether the two OID descriptors contain equal identifier bytes. */
int cpkt_gss_oid_equal(const cpkt_gss_oid *left, const cpkt_gss_oid *right);

/** Returns an owned buffer set for the mechanism-specific context query. */
cpkt_gss_status cpkt_gss_inquire_context_by_oid(cpkt_gss_status *minor_out,
                                                const cpkt_gss_context *context,
                                                const cpkt_gss_oid *object,
                                                cpkt_gss_buffer_set **data_out);
/** Sets a provider-specific context option using borrowed value bytes. */
cpkt_gss_status cpkt_gss_set_context_option(cpkt_gss_status *minor_out,
                                            cpkt_gss_context **context,
                                            const cpkt_gss_oid *option,
                                            const cpkt_gss_buffer *value);
/** Wraps plaintext with associated data into an owned token buffer. */
cpkt_gss_status
cpkt_gss_wrap_aead(cpkt_gss_status *minor_out, const cpkt_gss_context *context,
                   int confidentiality_requested, cpkt_gss_qop qop,
                   const cpkt_gss_buffer *associated_data,
                   const cpkt_gss_buffer *plaintext, int *confidentiality_out,
                   cpkt_gss_buffer *token_out);
/** Verifies associated data and unwraps a token into an owned plaintext buffer.
 */
cpkt_gss_status cpkt_gss_unwrap_aead(cpkt_gss_status *minor_out,
                                     const cpkt_gss_context *context,
                                     const cpkt_gss_buffer *token,
                                     const cpkt_gss_buffer *associated_data,
                                     cpkt_gss_buffer *plaintext_out,
                                     int *confidentiality_out,
                                     cpkt_gss_qop *qop_out);
/** Encapsulation has no provider minor status. Output is released as a GSS
 * buffer. */
cpkt_gss_status cpkt_gss_encapsulate_token(const cpkt_gss_buffer *input,
                                           const cpkt_gss_oid *oid,
                                           cpkt_gss_buffer *output);
/** Extracts a token payload into an owned GSS buffer. */
cpkt_gss_status cpkt_gss_decapsulate_token(const cpkt_gss_buffer *input,
                                           const cpkt_gss_oid *oid,
                                           cpkt_gss_buffer *output);

/** Serializes the credential into an owned token buffer. */
cpkt_gss_status
cpkt_gss_export_credential(cpkt_gss_status *minor_out,
                           const cpkt_gss_credential *credential,
                           cpkt_gss_buffer *token_out);
/** Imports a credential token into an owned credential handle. */
cpkt_gss_status
cpkt_gss_import_credential(cpkt_gss_status *minor_out,
                           const cpkt_gss_buffer *token,
                           cpkt_gss_credential **credential_out);
/** Maps the principal name to a local numeric user ID. */
cpkt_gss_status cpkt_gss_principal_name_to_uid(cpkt_gss_status *minor_out,
                                               const cpkt_gss_name *name,
                                               const cpkt_gss_oid *mechanism,
                                               unsigned long *uid_out);
/** Mechanism SPI invocation may update the supplied buffer descriptor.
 * Its contents and ownership are defined by the selected mechanism/OID. */
cpkt_gss_status cpkt_gss_mechanism_invoke(cpkt_gss_status *minor_out,
                                          const cpkt_gss_oid *mechanism,
                                          const cpkt_gss_oid *operation,
                                          cpkt_gss_buffer *value_in_out);

/** Legacy GSS seal/sign entry points retain native QOP and output ownership. */
cpkt_gss_status cpkt_gss_seal(cpkt_gss_status *minor_out,
                              const cpkt_gss_context *context, int conf,
                              int qop, const cpkt_gss_buffer *input,
                              int *conf_out, cpkt_gss_buffer *output);
/** Unwraps a legacy GSS token into an owned buffer and reports confidentiality.
 */
cpkt_gss_status cpkt_gss_unseal(cpkt_gss_status *minor_out,
                                const cpkt_gss_context *context,
                                const cpkt_gss_buffer *input,
                                cpkt_gss_buffer *output, int *conf_out,
                                int *qop_out);
/** Generates a legacy GSS signature in an owned token buffer. */
cpkt_gss_status cpkt_gss_sign(cpkt_gss_status *minor_out,
                              const cpkt_gss_context *context, int qop,
                              const cpkt_gss_buffer *message,
                              cpkt_gss_buffer *token_out);
/** Verifies a legacy GSS signature and reports the selected QOP. */
cpkt_gss_status cpkt_gss_verify(cpkt_gss_status *minor_out,
                                const cpkt_gss_context *context,
                                const cpkt_gss_buffer *message,
                                const cpkt_gss_buffer *token, int *qop_out);

/** Kerberos OIDs are borrowed for the entire provider lifetime. */
const cpkt_gss_oid *cpkt_gss_krb5_mechanism(void);
/** Returns the borrowed Kerberos principal name-type OID. */
const cpkt_gss_oid *cpkt_gss_krb5_principal_name_type(void);
/** Export consumes the context handle on success; on provider error the
 * context remains caller-owned. On success the
 * returned view owns native key bytes until
 * cpkt_gss_krb5_free_lucid_context(). */
cpkt_gss_status
cpkt_gss_krb5_export_lucid_context(cpkt_gss_status *minor_out,
                                   cpkt_gss_context **context,
                                   cpkt_gss_lucid_context **lucid_out);
/** Releases and clears the view on success. On provider error the view and
 * borrowed key bytes remain valid and caller-owned for a retry. */
cpkt_gss_status
cpkt_gss_krb5_free_lucid_context(cpkt_gss_status *minor_out,
                                 cpkt_gss_lucid_context **lucid);

/** Kerberos helper context. Close child handles before freeing it. Statuses
 * from these functions are native Kerberos error codes. */
int cpkt_gss_krb_context_new(cpkt_gss_krb_context **context_out);
/** Frees the Kerberos helper context after its child handles are closed. */
void cpkt_gss_krb_context_free(cpkt_gss_krb_context **context);
/** Resolves an owned Kerberos cache handle; close it with its helper context.
 */
int cpkt_gss_krb_cache_resolve(cpkt_gss_krb_context *context, const char *name,
                               cpkt_gss_krb_cache **cache_out);
/** Closes the Kerberos cache handle using its helper context. */
int cpkt_gss_krb_cache_close(cpkt_gss_krb_context *context,
                             cpkt_gss_krb_cache **cache);
/** Resolves an owned Kerberos keytab handle; close it with its helper context.
 */
int cpkt_gss_krb_keytab_resolve(cpkt_gss_krb_context *context, const char *name,
                                cpkt_gss_krb_keytab **keytab_out);
/** Closes the Kerberos keytab handle using its helper context. */
int cpkt_gss_krb_keytab_close(cpkt_gss_krb_context *context,
                              cpkt_gss_krb_keytab **keytab);
/** Parses an owned Kerberos principal; free it with its helper context. */
int cpkt_gss_krb_principal_parse(cpkt_gss_krb_context *context,
                                 const char *name,
                                 cpkt_gss_krb_principal **principal_out);
/** Frees a parsed Kerberos principal using its helper context. */
void cpkt_gss_krb_principal_free(cpkt_gss_krb_context *context,
                                 cpkt_gss_krb_principal **principal);
/** Resolves the default replay cache. A successful set_credential transfers
 * its ownership to the GSS credential and clears the caller's handle. */
int cpkt_gss_krb_replay_cache_new(cpkt_gss_krb_context *context,
                                  cpkt_gss_krb_replay_cache **replay_cache_out);
/** Frees a replay cache that has not been transferred to a GSS credential. */
void cpkt_gss_krb_replay_cache_free(cpkt_gss_krb_context *context,
                                    cpkt_gss_krb_replay_cache **replay_cache);
/** Transfers the replay cache to the credential on success and clears its
 * handle. */
cpkt_gss_status cpkt_gss_krb5_set_credential_replay_cache(
    cpkt_gss_status *minor_out, cpkt_gss_krb_context *context,
    cpkt_gss_credential *credential, cpkt_gss_krb_replay_cache **replay_cache);

/** The Kerberos-specific GSS calls consume borrowed Kerberos handles for the
 * duration of the operation; the caller still closes those handles. */
cpkt_gss_status cpkt_gss_krb5_import_credential(
    cpkt_gss_status *minor_out, cpkt_gss_krb_cache *cache,
    cpkt_gss_krb_principal *principal, cpkt_gss_krb_keytab *keytab,
    cpkt_gss_credential **credential_out);
/** Copies the credential's Kerberos tickets into the borrowed cache. */
cpkt_gss_status cpkt_gss_krb5_copy_cache(cpkt_gss_status *minor_out,
                                         const cpkt_gss_credential *credential,
                                         cpkt_gss_krb_cache *cache);
/** Sets the credential's permitted Kerberos encryption types. */
cpkt_gss_status cpkt_gss_krb5_set_enctypes(cpkt_gss_status *minor_out,
                                           cpkt_gss_credential *credential,
                                           const long *enctypes, size_t count);
/** Reports the Kerberos ticket flags associated with the context. */
cpkt_gss_status cpkt_gss_krb5_get_ticket_flags(cpkt_gss_status *minor_out,
                                               const cpkt_gss_context *context,
                                               unsigned long *flags_out);
/** Reports the Kerberos authentication timestamp associated with the context.
 */
cpkt_gss_status
cpkt_gss_krb5_extract_authentication_time(cpkt_gss_status *minor_out,
                                          const cpkt_gss_context *context,
                                          long *timestamp_out);
/** Copies the requested authorization data into an owned GSS buffer. */
cpkt_gss_status cpkt_gss_krb5_extract_authorization_data(
    cpkt_gss_status *minor_out, const cpkt_gss_context *context, int data_type,
    cpkt_gss_buffer *data_out);
/** Selects the credential cache name and returns the provider's previous name.
 */
cpkt_gss_status cpkt_gss_krb5_cache_name(cpkt_gss_status *minor_out,
                                         const char *name,
                                         const char **previous_name_out);
/** Registers the keytab identity used by subsequent acceptor operations. */
cpkt_gss_status
cpkt_gss_krb5_register_acceptor_identity(const char *keytab_name);
/** The name must outlive the mapped PAC. A mapping may be absent when its
 * authenticated PAC attribute was not present on the name. */
cpkt_gss_status cpkt_gss_map_name_to_pac(cpkt_gss_status *minor_out,
                                         cpkt_gss_name *name,
                                         int authenticated_only,
                                         cpkt_gss_pac **pac_out);
/** Releases a PAC mapping; its associated name must remain alive until release.
 */
cpkt_gss_status cpkt_gss_pac_release(cpkt_gss_status *minor_out,
                                     cpkt_gss_pac **pac);
/** PAC helpers return Kerberos error codes. Arrays and byte buffers are owned
 * by the caller and released with matching free helpers. */
int cpkt_gss_pac_types(cpkt_gss_krb_context *context, const cpkt_gss_pac *pac,
                       unsigned long **types_out, size_t *count_out);
/** Frees an owned PAC type array returned by cpkt_gss_pac_types(). */
void cpkt_gss_pac_types_free(unsigned long **types);
/** Copies the selected PAC buffer into caller-owned storage. */
int cpkt_gss_pac_get_buffer(cpkt_gss_krb_context *context,
                            const cpkt_gss_pac *pac, unsigned long type,
                            cpkt_gss_buffer *buffer_out);
/** Frees PAC buffer storage returned by cpkt_gss_pac_get_buffer(). */
void cpkt_gss_pac_buffer_free(cpkt_gss_buffer *buffer);

/** @} */
#endif
