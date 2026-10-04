#ifndef CPKT_OPENSSL_H
#define CPKT_OPENSSL_H

/*
 * Complete C89-compatible import surface for the OpenSSL headers bundled by
 * cpkt.  OpenSSL's legacy SHA-512 state declares long long despite
 * otherwise accepting strict C89; contain that upstream diagnostic here so a
 * consumer's own source remains warning-clean under -std=c89 -pedantic-errors.
 * Typed cpkt_openssl_* adapters provide C89 word values for APIs that accept
 * or return native 64-bit values.
 */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wlong-long"
#endif

#if defined(_WIN32) && defined(CPKT_OPENSSL_BUILDING_SHARED)
#define CPKT_OPENSSL_API __declspec(dllexport)
#elif defined(_WIN32) && !defined(CPKT_OPENSSL_STATIC)
#define CPKT_OPENSSL_API __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define CPKT_OPENSSL_API __attribute__((visibility("default")))
#else
#define CPKT_OPENSSL_API
#endif

#include <openssl/aes.h>
#include <openssl/asn1.h>
#include <openssl/asn1err.h>
#include <openssl/asn1t.h>
#include <openssl/async.h>
#include <openssl/asyncerr.h>
#include <openssl/bio.h>
#include <openssl/bioerr.h>
#include <openssl/blowfish.h>
#include <openssl/bn.h>
#include <openssl/bnerr.h>
#include <openssl/buffer.h>
#include <openssl/buffererr.h>
#include <openssl/byteorder.h>
#include <openssl/camellia.h>
#include <openssl/cast.h>
#include <openssl/cmac.h>
#include <openssl/cmp.h>
#include <openssl/cmp_util.h>
#include <openssl/cmperr.h>
#include <openssl/cms.h>
#include <openssl/cmserr.h>
#include <openssl/comp.h>
#include <openssl/comperr.h>
#include <openssl/conf.h>
#include <openssl/conf_api.h>
#include <openssl/conferr.h>
#include <openssl/configuration.h>
#include <openssl/conftypes.h>
#include <openssl/core.h>
#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/core_object.h>
#include <openssl/crmf.h>
#include <openssl/crmferr.h>
#include <openssl/crypto.h>
#include <openssl/cryptoerr.h>
#include <openssl/cryptoerr_legacy.h>
#include <openssl/ct.h>
#include <openssl/cterr.h>
#include <openssl/decoder.h>
#include <openssl/decodererr.h>
#include <openssl/des.h>
#include <openssl/dh.h>
#include <openssl/dherr.h>
#include <openssl/dsa.h>
#include <openssl/dsaerr.h>
#include <openssl/dtls1.h>
#include <openssl/e_os2.h>
#include <openssl/e_ostime.h>
#include <openssl/ebcdic.h>
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/ecdsa.h>
#include <openssl/ecerr.h>
#include <openssl/encoder.h>
#include <openssl/encodererr.h>
#include <openssl/engine.h>
#include <openssl/engineerr.h>
#include <openssl/err.h>
#include <openssl/ess.h>
#include <openssl/esserr.h>
#include <openssl/evp.h>
#include <openssl/evperr.h>
#include <openssl/fips_names.h>
#include <openssl/fipskey.h>
#include <openssl/hmac.h>
#include <openssl/hpke.h>
#include <openssl/http.h>
#include <openssl/httperr.h>
#include <openssl/idea.h>
#include <openssl/indicator.h>
#include <openssl/kdf.h>
#include <openssl/kdferr.h>
#include <openssl/lhash.h>
#include <openssl/macros.h>
#include <openssl/md2.h>
#include <openssl/md4.h>
#include <openssl/md5.h>
#include <openssl/mdc2.h>
#include <openssl/ml_kem.h>
#include <openssl/modes.h>
#include <openssl/obj_mac.h>
#include <openssl/objects.h>
#include <openssl/objectserr.h>
#include <openssl/ocsp.h>
#include <openssl/ocsperr.h>
#include <openssl/opensslconf.h>
#include <openssl/opensslv.h>
#include <openssl/ossl_typ.h>
#include <openssl/param_build.h>
#include <openssl/params.h>
#include <openssl/pem.h>
#include <openssl/pem2.h>
#include <openssl/pemerr.h>
#include <openssl/pkcs12.h>
#include <openssl/pkcs12err.h>
#include <openssl/pkcs7.h>
#include <openssl/pkcs7err.h>
#include <openssl/prov_ssl.h>
#include <openssl/proverr.h>
#include <openssl/provider.h>
#include <openssl/quic.h>
#include <openssl/rand.h>
#include <openssl/randerr.h>
#include <openssl/rc2.h>
#include <openssl/rc4.h>
#include <openssl/rc5.h>
#include <openssl/ripemd.h>
#include <openssl/rsa.h>
#include <openssl/rsaerr.h>
#include <openssl/safestack.h>
#include <openssl/seed.h>
#include <openssl/self_test.h>
#include <openssl/sha.h>
#include <openssl/srp.h>
#include <openssl/srtp.h>
#include <openssl/ssl.h>
#include <openssl/ssl2.h>
#include <openssl/ssl3.h>
#include <openssl/sslerr.h>
#include <openssl/sslerr_legacy.h>
#include <openssl/stack.h>
#include <openssl/store.h>
#include <openssl/storeerr.h>
#include <openssl/symhacks.h>
#include <openssl/thread.h>
#include <openssl/tls1.h>
#include <openssl/trace.h>
#include <openssl/ts.h>
#include <openssl/tserr.h>
#include <openssl/txt_db.h>
#include <openssl/types.h>
#include <openssl/ui.h>
#include <openssl/uierr.h>
#include <openssl/whrlpool.h>
#include <openssl/x509.h>
#include <openssl/x509_acert.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509err.h>
#include <openssl/x509v3.h>
#include <openssl/x509v3err.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

/* Exact unsigned 64-bit bits represented without a C99 scalar. The byte
 * storage is exactly eight bytes so it can back upstream pointer APIs. */
/** Exact unsigned 64-bit bits in eight bytes; use the C89 word helpers. */
typedef struct cpkt_openssl_u64 {
  unsigned char bytes[8];
} cpkt_openssl_u64;

/* Exact signed 64-bit two's-complement bits in exactly eight bytes. */
/** Exact signed 64-bit bits in eight bytes; use the C89 word helpers. */
typedef struct cpkt_openssl_i64 {
  unsigned char bytes[8];
} cpkt_openssl_i64;

/** Owned, aligned native storage for one signed OpenSSL parameter. */
typedef struct cpkt_openssl_param_i64 cpkt_openssl_param_i64;
/** Owned, aligned native storage for one unsigned OpenSSL parameter. */
typedef struct cpkt_openssl_param_u64 cpkt_openssl_param_u64;
/** Opaque C89 shell for OpenSSL's SHA-384/SHA-512 native state. */
typedef struct cpkt_openssl_sha512_context cpkt_openssl_sha512_context;
/** Opaque aligned native storage for OpenSSL atomic 64-bit operations. */
typedef struct cpkt_openssl_atomic_u64 cpkt_openssl_atomic_u64;
/** Owned C89 shell for a custom BIO_METHOD with batch callbacks. */
typedef struct cpkt_openssl_bio_method cpkt_openssl_bio_method;
/** Owned C89 shell for a BIO that uses a cpkt_openssl_bio_method. */
typedef struct cpkt_openssl_bio cpkt_openssl_bio;
/** C89 form of SSL_SHUTDOWN_EX_ARGS. */
typedef struct cpkt_openssl_ssl_shutdown_args {
  cpkt_openssl_u64 quic_error_code;
  const char *quic_reason;
} cpkt_openssl_ssl_shutdown_args;
/** C89 form of SSL_STREAM_RESET_ARGS. */
typedef struct cpkt_openssl_ssl_stream_reset_args {
  cpkt_openssl_u64 quic_error_code;
} cpkt_openssl_ssl_stream_reset_args;
/** C89 form of SSL_CONN_CLOSE_INFO. */
typedef struct cpkt_openssl_ssl_conn_close_info {
  cpkt_openssl_u64 error_code;
  cpkt_openssl_u64 frame_type;
  const char *reason;
  size_t reason_length;
  unsigned long flags;
} cpkt_openssl_ssl_conn_close_info;
/** C89 form of BIO_MSG for datagram batch operations. */
typedef struct cpkt_openssl_bio_message {
  void *data;
  size_t data_length;
  BIO_ADDR *peer;
  BIO_ADDR *local;
  cpkt_openssl_u64 flags;
} cpkt_openssl_bio_message;
/** C89 form of BIO_MMSG_CB_ARGS, borrowed for the current callback. The
 * descriptor is read-only; message records may be changed. Record changes
 * propagate to the batch method and caller, including when a callback rejects
 * the operation. Pointer fields remain borrowed; keep replacement storage
 * valid for the operation and any subsequent caller use. */
typedef struct cpkt_openssl_bio_mmsg_callback_args {
  cpkt_openssl_bio_message *messages;
  size_t message_stride;
  size_t message_count;
  cpkt_openssl_u64 flags;
  size_t *processed_out;
} cpkt_openssl_bio_mmsg_callback_args;
/** C89 batch callback for a facade-owned BIO method. */
typedef int (*cpkt_openssl_bio_mmsg_callback)(
    void *context, BIO *bio, cpkt_openssl_bio_message *messages,
    size_t message_stride, size_t message_count, cpkt_openssl_u64 flags,
    size_t *processed_out);
/** C89 form of BIO_callback_fn for a facade-owned BIO. */
typedef long (*cpkt_openssl_bio_callback)(void *context, BIO *bio,
                                          int operation, const void *argument,
                                          int argument_integer,
                                          long argument_long, long result);
/** C89 form of BIO_callback_fn_ex for a facade-owned BIO. */
typedef long (*cpkt_openssl_bio_callback_ex)(
    void *context, BIO *bio, int operation, const void *argument,
    size_t argument_length, int argument_integer, long argument_long,
    int result, size_t *processed_out);
/* Values for cpkt_openssl_poll_descriptor.value_kind. */
#define CPKT_OPENSSL_POLL_VALUE_NONE 0UL
#define CPKT_OPENSSL_POLL_VALUE_FD 1UL
#define CPKT_OPENSSL_POLL_VALUE_CUSTOM_POINTER 2UL
#define CPKT_OPENSSL_POLL_VALUE_CUSTOM_UINTPTR 3UL
#define CPKT_OPENSSL_POLL_VALUE_SSL 4UL
/** C89 form of BIO_POLL_DESCRIPTOR. Zero initialization means no descriptor. */
typedef struct cpkt_openssl_poll_descriptor {
  unsigned long type;
  unsigned long value_kind;
  int file_descriptor;
  void *custom_pointer;
  cpkt_openssl_u64 custom_uintptr;
  SSL *ssl;
} cpkt_openssl_poll_descriptor;
/** C89 form of SSL_POLL_ITEM. */
typedef struct cpkt_openssl_ssl_poll_item {
  cpkt_openssl_poll_descriptor descriptor;
  cpkt_openssl_u64 events;
  cpkt_openssl_u64 returned_events;
} cpkt_openssl_ssl_poll_item;

/** Constructs an exact unsigned 64-bit value from two 32-bit words. */
CPKT_OPENSSL_API cpkt_openssl_u64 cpkt_openssl_u64_make(unsigned long high,
                                                        unsigned long low);
/** Returns the most-significant 32-bit word of an exact unsigned value. */
CPKT_OPENSSL_API unsigned long
cpkt_openssl_u64_high_word(cpkt_openssl_u64 value);
/** Returns the least-significant 32-bit word of an exact unsigned value. */
CPKT_OPENSSL_API unsigned long
cpkt_openssl_u64_low_word(cpkt_openssl_u64 value);
/** Compares both words of two C89 unsigned 64-bit values. */
CPKT_OPENSSL_API int cpkt_openssl_u64_equal(cpkt_openssl_u64 left,
                                            cpkt_openssl_u64 right);
/** Returns nonzero when both 32-bit words are zero. */
CPKT_OPENSSL_API int cpkt_openssl_u64_is_zero(cpkt_openssl_u64 value);
/** Creates exact signed 64-bit two's-complement bits from two 32-bit words. */
CPKT_OPENSSL_API cpkt_openssl_i64 cpkt_openssl_i64_make(unsigned long high,
                                                        unsigned long low);
/** Returns the most-significant two's-complement word of a signed value. */
CPKT_OPENSSL_API unsigned long
cpkt_openssl_i64_high_word(cpkt_openssl_i64 value);
/** Returns the least-significant two's-complement word of a signed value. */
CPKT_OPENSSL_API unsigned long
cpkt_openssl_i64_low_word(cpkt_openssl_i64 value);
/** Compares exact signed 64-bit two's-complement bits. */
CPKT_OPENSSL_API int cpkt_openssl_i64_equal(cpkt_openssl_i64 left,
                                            cpkt_openssl_i64 right);

/** C89 adapter for OPENSSL_init_crypto. */
CPKT_OPENSSL_API int
cpkt_openssl_OPENSSL_init_crypto(cpkt_openssl_u64 options,
                                 const OPENSSL_INIT_SETTINGS *settings);
/** C89 adapter for OPENSSL_init_ssl. */
CPKT_OPENSSL_API int
cpkt_openssl_OPENSSL_init_ssl(cpkt_openssl_u64 options,
                              const OPENSSL_INIT_SETTINGS *settings);
/** C89 adapter for SSL_CTX_get_options. */
CPKT_OPENSSL_API cpkt_openssl_u64
cpkt_openssl_SSL_CTX_get_options(const SSL_CTX *context);
/** C89 adapter for SSL_get_options. */
CPKT_OPENSSL_API cpkt_openssl_u64 cpkt_openssl_SSL_get_options(const SSL *ssl);
/** C89 adapter for SSL_CTX_set_options. */
CPKT_OPENSSL_API cpkt_openssl_u64
cpkt_openssl_SSL_CTX_set_options(SSL_CTX *context, cpkt_openssl_u64 options);
/** C89 adapter for SSL_set_options. */
CPKT_OPENSSL_API cpkt_openssl_u64
cpkt_openssl_SSL_set_options(SSL *ssl, cpkt_openssl_u64 options);
/** C89 adapter for SSL_CTX_clear_options. */
CPKT_OPENSSL_API cpkt_openssl_u64
cpkt_openssl_SSL_CTX_clear_options(SSL_CTX *context, cpkt_openssl_u64 options);
/** C89 adapter for SSL_clear_options. */
CPKT_OPENSSL_API cpkt_openssl_u64
cpkt_openssl_SSL_clear_options(SSL *ssl, cpkt_openssl_u64 options);
/** C89 adapter for SSL_get_handshake_rtt. */
CPKT_OPENSSL_API int
cpkt_openssl_SSL_get_handshake_rtt(const SSL *ssl, cpkt_openssl_u64 *rtt_out);

/** C89 adapter for ASN1_ENUMERATED_get_int64. */
CPKT_OPENSSL_API int
cpkt_openssl_ASN1_ENUMERATED_get_int64(cpkt_openssl_i64 *value_out,
                                       const ASN1_ENUMERATED *enumerated);
/** C89 adapter for ASN1_ENUMERATED_set_int64. */
CPKT_OPENSSL_API int
cpkt_openssl_ASN1_ENUMERATED_set_int64(ASN1_ENUMERATED *enumerated,
                                       cpkt_openssl_i64 value);
/** C89 adapter for ASN1_INTEGER_get_int64. */
CPKT_OPENSSL_API int
cpkt_openssl_ASN1_INTEGER_get_int64(cpkt_openssl_i64 *value_out,
                                    const ASN1_INTEGER *integer);
/** C89 adapter for ASN1_INTEGER_get_uint64. */
CPKT_OPENSSL_API int
cpkt_openssl_ASN1_INTEGER_get_uint64(cpkt_openssl_u64 *value_out,
                                     const ASN1_INTEGER *integer);
/** C89 adapter for ASN1_INTEGER_set_int64. */
CPKT_OPENSSL_API int
cpkt_openssl_ASN1_INTEGER_set_int64(ASN1_INTEGER *integer,
                                    cpkt_openssl_i64 value);
/** C89 adapter for ASN1_INTEGER_set_uint64. */
CPKT_OPENSSL_API int
cpkt_openssl_ASN1_INTEGER_set_uint64(ASN1_INTEGER *integer,
                                     cpkt_openssl_u64 value);
/** C89 adapter for CT_POLICY_EVAL_CTX_get_time. */
CPKT_OPENSSL_API cpkt_openssl_u64
cpkt_openssl_CT_POLICY_EVAL_CTX_get_time(const CT_POLICY_EVAL_CTX *context);
/** C89 adapter for CT_POLICY_EVAL_CTX_set_time. */
CPKT_OPENSSL_API void
cpkt_openssl_CT_POLICY_EVAL_CTX_set_time(CT_POLICY_EVAL_CTX *context,
                                         cpkt_openssl_u64 value);
/** C89 adapter for OSSL_get_max_threads. */
CPKT_OPENSSL_API cpkt_openssl_u64
cpkt_openssl_OSSL_get_max_threads(OSSL_LIB_CTX *library_context);
/** C89 adapter for OSSL_set_max_threads. */
CPKT_OPENSSL_API int
cpkt_openssl_OSSL_set_max_threads(OSSL_LIB_CTX *library_context,
                                  cpkt_openssl_u64 value);
/** C89 adapter for OSSL_sleep. */
CPKT_OPENSSL_API void cpkt_openssl_OSSL_sleep(cpkt_openssl_u64 milliseconds);
/** C89 adapter for SCT_get_timestamp. */
CPKT_OPENSSL_API cpkt_openssl_u64
cpkt_openssl_SCT_get_timestamp(const SCT *sct);
/** C89 adapter for SCT_new_from_base64. */
CPKT_OPENSSL_API SCT *cpkt_openssl_SCT_new_from_base64(
    unsigned char version, const char *log_id, ct_log_entry_type_t entry_type,
    cpkt_openssl_u64 timestamp, const char *extensions, const char *signature);
/** C89 adapter for SCT_set_timestamp. */
CPKT_OPENSSL_API void
cpkt_openssl_SCT_set_timestamp(SCT *sct, cpkt_openssl_u64 timestamp);
/** C89 adapter for EVP_PBE_scrypt. */
CPKT_OPENSSL_API int cpkt_openssl_EVP_PBE_scrypt(
    const char *password, size_t password_length, const unsigned char *salt,
    size_t salt_length, cpkt_openssl_u64 work_factor,
    cpkt_openssl_u64 block_size, cpkt_openssl_u64 parallelization,
    cpkt_openssl_u64 maximum_memory, unsigned char *key, size_t key_length);
/** C89 adapter for EVP_PBE_scrypt_ex. */
CPKT_OPENSSL_API int cpkt_openssl_EVP_PBE_scrypt_ex(
    const char *password, size_t password_length, const unsigned char *salt,
    size_t salt_length, cpkt_openssl_u64 work_factor,
    cpkt_openssl_u64 block_size, cpkt_openssl_u64 parallelization,
    cpkt_openssl_u64 maximum_memory, unsigned char *key, size_t key_length,
    OSSL_LIB_CTX *library_context, const char *property_query);
/** C89 adapter for EVP_PKEY_CTX_ctrl_uint64. */
CPKT_OPENSSL_API int
cpkt_openssl_EVP_PKEY_CTX_ctrl_uint64(EVP_PKEY_CTX *context, int key_type,
                                      int operation, int command,
                                      cpkt_openssl_u64 value);
/** C89 adapter for EVP_PKEY_CTX_set_scrypt_N. */
CPKT_OPENSSL_API int
cpkt_openssl_EVP_PKEY_CTX_set_scrypt_N(EVP_PKEY_CTX *context,
                                       cpkt_openssl_u64 work_factor);
/** C89 adapter for EVP_PKEY_CTX_set_scrypt_maxmem_bytes. */
CPKT_OPENSSL_API int cpkt_openssl_EVP_PKEY_CTX_set_scrypt_maxmem_bytes(
    EVP_PKEY_CTX *context, cpkt_openssl_u64 maximum_memory);
/** C89 adapter for EVP_PKEY_CTX_set_scrypt_p. */
CPKT_OPENSSL_API int
cpkt_openssl_EVP_PKEY_CTX_set_scrypt_p(EVP_PKEY_CTX *context,
                                       cpkt_openssl_u64 parallelization);
/** C89 adapter for EVP_PKEY_CTX_set_scrypt_r. */
CPKT_OPENSSL_API int
cpkt_openssl_EVP_PKEY_CTX_set_scrypt_r(EVP_PKEY_CTX *context,
                                       cpkt_openssl_u64 block_size);
/** C89 adapter for PKCS5_pbe2_set_scrypt. */
CPKT_OPENSSL_API X509_ALGOR *cpkt_openssl_PKCS5_pbe2_set_scrypt(
    const EVP_CIPHER *cipher, const unsigned char *salt, int salt_length,
    unsigned char *iv, cpkt_openssl_u64 work_factor,
    cpkt_openssl_u64 block_size, cpkt_openssl_u64 parallelization);
/** C89 adapter for SSL_CTX_get_domain_flags. */
CPKT_OPENSSL_API int
cpkt_openssl_SSL_CTX_get_domain_flags(const SSL_CTX *context,
                                      cpkt_openssl_u64 *flags_out);
/** C89 adapter for SSL_CTX_set_domain_flags. */
CPKT_OPENSSL_API int
cpkt_openssl_SSL_CTX_set_domain_flags(SSL_CTX *context, cpkt_openssl_u64 flags);
/** C89 adapter for SSL_get_domain_flags. */
CPKT_OPENSSL_API int
cpkt_openssl_SSL_get_domain_flags(const SSL *ssl, cpkt_openssl_u64 *flags_out);
/** C89 adapter for SSL_accept_connection. */
CPKT_OPENSSL_API SSL *
cpkt_openssl_SSL_accept_connection(SSL *ssl, cpkt_openssl_u64 domain_flags);
/** C89 adapter for SSL_accept_stream. */
CPKT_OPENSSL_API SSL *
cpkt_openssl_SSL_accept_stream(SSL *ssl, cpkt_openssl_u64 stream_id);
/** C89 adapter for SSL_get_stream_id. */
CPKT_OPENSSL_API cpkt_openssl_u64 cpkt_openssl_SSL_get_stream_id(SSL *ssl);
/** C89 adapter for SSL_get_stream_read_error_code. */
CPKT_OPENSSL_API int
cpkt_openssl_SSL_get_stream_read_error_code(SSL *ssl,
                                            cpkt_openssl_u64 *error_code_out);
/** C89 adapter for SSL_get_stream_write_error_code. */
CPKT_OPENSSL_API int
cpkt_openssl_SSL_get_stream_write_error_code(SSL *ssl,
                                             cpkt_openssl_u64 *error_code_out);
/** C89 adapter for SSL_new_domain. */
CPKT_OPENSSL_API SSL *
cpkt_openssl_SSL_new_domain(SSL_CTX *context, cpkt_openssl_u64 domain_flags);
/** C89 adapter for SSL_new_from_listener. */
CPKT_OPENSSL_API SSL *
cpkt_openssl_SSL_new_from_listener(SSL *listener, cpkt_openssl_u64 stream_id);
/** C89 adapter for SSL_new_listener. */
CPKT_OPENSSL_API SSL *
cpkt_openssl_SSL_new_listener(SSL_CTX *context, cpkt_openssl_u64 domain_flags);
/** C89 adapter for SSL_new_listener_from. */
CPKT_OPENSSL_API SSL *
cpkt_openssl_SSL_new_listener_from(SSL *ssl, cpkt_openssl_u64 domain_flags);
/** C89 adapter for SSL_new_stream. */
CPKT_OPENSSL_API SSL *cpkt_openssl_SSL_new_stream(SSL *ssl,
                                                  cpkt_openssl_u64 stream_id);
/** C89 adapter for SSL_set_incoming_stream_policy. */
CPKT_OPENSSL_API int cpkt_openssl_SSL_set_incoming_stream_policy(
    SSL *ssl, int policy, cpkt_openssl_u64 application_error_code);
/** C89 adapter for SSL_stream_conclude. */
CPKT_OPENSSL_API int
cpkt_openssl_SSL_stream_conclude(SSL *ssl,
                                 cpkt_openssl_u64 application_error_code);
/** C89 adapter for SSL_write_ex2. */
CPKT_OPENSSL_API int cpkt_openssl_SSL_write_ex2(SSL *ssl, const void *buffer,
                                                size_t buffer_length,
                                                cpkt_openssl_u64 flags,
                                                size_t *written_out);
/** C89 adapter for BIO_number_read. */
CPKT_OPENSSL_API cpkt_openssl_u64 cpkt_openssl_BIO_number_read(BIO *bio);
/** C89 adapter for BIO_number_written. */
CPKT_OPENSSL_API cpkt_openssl_u64 cpkt_openssl_BIO_number_written(BIO *bio);
/** C89 adapter for OSSL_HPKE_CTX_get_seq. */
CPKT_OPENSSL_API int
cpkt_openssl_OSSL_HPKE_CTX_get_seq(OSSL_HPKE_CTX *context,
                                   cpkt_openssl_u64 *sequence_out);
/** C89 adapter for OSSL_HPKE_CTX_set_seq. */
CPKT_OPENSSL_API int
cpkt_openssl_OSSL_HPKE_CTX_set_seq(OSSL_HPKE_CTX *context,
                                   cpkt_openssl_u64 sequence);
/** C89 adapter for OSSL_PARAM_BLD_push_int64. */
CPKT_OPENSSL_API int
cpkt_openssl_OSSL_PARAM_BLD_push_int64(OSSL_PARAM_BLD *builder, const char *key,
                                       cpkt_openssl_i64 value);
/** C89 adapter for OSSL_PARAM_BLD_push_uint64. */
CPKT_OPENSSL_API int cpkt_openssl_OSSL_PARAM_BLD_push_uint64(
    OSSL_PARAM_BLD *builder, const char *key, cpkt_openssl_u64 value);
/** Creates aligned storage for the C89 form of OSSL_PARAM_construct_int64. */
CPKT_OPENSSL_API int
cpkt_openssl_OSSL_PARAM_construct_int64(cpkt_openssl_param_i64 **parameter_out,
                                        const char *key,
                                        cpkt_openssl_i64 value);
/** Creates aligned storage for the C89 form of OSSL_PARAM_construct_uint64. */
CPKT_OPENSSL_API int
cpkt_openssl_OSSL_PARAM_construct_uint64(cpkt_openssl_param_u64 **parameter_out,
                                         const char *key,
                                         cpkt_openssl_u64 value);
/** Returns the native parameter view valid until its shell is freed. */
CPKT_OPENSSL_API OSSL_PARAM *
cpkt_openssl_param_i64_native(cpkt_openssl_param_i64 *parameter);
/** Returns the current signed value held by an aligned parameter shell. */
CPKT_OPENSSL_API cpkt_openssl_i64
cpkt_openssl_param_i64_value(const cpkt_openssl_param_i64 *parameter);
/** Replaces the current signed value held by an aligned parameter shell. */
CPKT_OPENSSL_API void
cpkt_openssl_param_i64_set_value(cpkt_openssl_param_i64 *parameter,
                                 cpkt_openssl_i64 value);
/** Releases an aligned signed parameter shell. */
CPKT_OPENSSL_API void
cpkt_openssl_param_i64_free(cpkt_openssl_param_i64 *parameter);
/** Returns the native parameter view valid until its shell is freed. */
CPKT_OPENSSL_API OSSL_PARAM *
cpkt_openssl_param_u64_native(cpkt_openssl_param_u64 *parameter);
/** Returns the current unsigned value held by an aligned parameter shell. */
CPKT_OPENSSL_API cpkt_openssl_u64
cpkt_openssl_param_u64_value(const cpkt_openssl_param_u64 *parameter);
/** Replaces the current unsigned value held by an aligned parameter shell. */
CPKT_OPENSSL_API void
cpkt_openssl_param_u64_set_value(cpkt_openssl_param_u64 *parameter,
                                 cpkt_openssl_u64 value);
/** Releases an aligned unsigned parameter shell. */
CPKT_OPENSSL_API void
cpkt_openssl_param_u64_free(cpkt_openssl_param_u64 *parameter);
/** C89 adapter for OSSL_PARAM_get_int64. */
CPKT_OPENSSL_API int
cpkt_openssl_OSSL_PARAM_get_int64(const OSSL_PARAM *parameter,
                                  cpkt_openssl_i64 *value_out);
/** C89 adapter for OSSL_PARAM_get_uint64. */
CPKT_OPENSSL_API int
cpkt_openssl_OSSL_PARAM_get_uint64(const OSSL_PARAM *parameter,
                                   cpkt_openssl_u64 *value_out);
/** C89 adapter for OSSL_PARAM_set_int64. */
CPKT_OPENSSL_API int cpkt_openssl_OSSL_PARAM_set_int64(OSSL_PARAM *parameter,
                                                       cpkt_openssl_i64 value);
/** C89 adapter for OSSL_PARAM_set_uint64. */
CPKT_OPENSSL_API int cpkt_openssl_OSSL_PARAM_set_uint64(OSSL_PARAM *parameter,
                                                        cpkt_openssl_u64 value);
/** Allocates a C89 shell for SHA-384 and SHA-512 operations. */
CPKT_OPENSSL_API cpkt_openssl_sha512_context *cpkt_openssl_SHA512_CTX_new(void);
/** Releases a C89 SHA-384/SHA-512 context shell. */
CPKT_OPENSSL_API void
cpkt_openssl_SHA512_CTX_free(cpkt_openssl_sha512_context *context);
/** C89 adapter for SHA384_Init. */
CPKT_OPENSSL_API int
cpkt_openssl_SHA384_Init(cpkt_openssl_sha512_context *context);
/** C89 adapter for SHA384_Update. */
CPKT_OPENSSL_API int
cpkt_openssl_SHA384_Update(cpkt_openssl_sha512_context *context,
                           const void *data, size_t length);
/** C89 adapter for SHA384_Final. */
CPKT_OPENSSL_API int
cpkt_openssl_SHA384_Final(unsigned char *digest,
                          cpkt_openssl_sha512_context *context);
/** C89 adapter for SHA512_Init. */
CPKT_OPENSSL_API int
cpkt_openssl_SHA512_Init(cpkt_openssl_sha512_context *context);
/** C89 adapter for SHA512_Update. */
CPKT_OPENSSL_API int
cpkt_openssl_SHA512_Update(cpkt_openssl_sha512_context *context,
                           const void *data, size_t length);
/** C89 adapter for SHA512_Final. */
CPKT_OPENSSL_API int
cpkt_openssl_SHA512_Final(unsigned char *digest,
                          cpkt_openssl_sha512_context *context);
/** C89 adapter for SHA512_Transform. */
CPKT_OPENSSL_API void
cpkt_openssl_SHA512_Transform(cpkt_openssl_sha512_context *context,
                              const unsigned char *block);
/** C89 adapter for SSL_get_value_uint. Selectors must fit 32 bits. */
CPKT_OPENSSL_API int
cpkt_openssl_SSL_get_value_uint(SSL *ssl, unsigned long value_class,
                                unsigned long value_id,
                                cpkt_openssl_u64 *value_out);
/** C89 adapter for SSL_set_value_uint. Selectors must fit 32 bits. */
CPKT_OPENSSL_API int cpkt_openssl_SSL_set_value_uint(SSL *ssl,
                                                     unsigned long value_class,
                                                     unsigned long value_id,
                                                     cpkt_openssl_u64 value);
/** Allocates an aligned C89 shell for OpenSSL atomic 64-bit operations. */
CPKT_OPENSSL_API cpkt_openssl_atomic_u64 *
cpkt_openssl_atomic_u64_new(cpkt_openssl_u64 initial_value);
/** Releases an OpenSSL atomic-value shell after concurrent use has stopped. */
CPKT_OPENSSL_API void
cpkt_openssl_atomic_u64_free(cpkt_openssl_atomic_u64 *value);
/** C89 adapter for CRYPTO_atomic_add64. */
CPKT_OPENSSL_API int cpkt_openssl_CRYPTO_atomic_add64(
    cpkt_openssl_atomic_u64 *value, cpkt_openssl_u64 amount,
    cpkt_openssl_u64 *result_out, CRYPTO_RWLOCK *lock);
/** C89 adapter for CRYPTO_atomic_and. */
CPKT_OPENSSL_API int cpkt_openssl_CRYPTO_atomic_and(
    cpkt_openssl_atomic_u64 *value, cpkt_openssl_u64 mask,
    cpkt_openssl_u64 *result_out, CRYPTO_RWLOCK *lock);
/** C89 adapter for CRYPTO_atomic_load. */
CPKT_OPENSSL_API int
cpkt_openssl_CRYPTO_atomic_load(cpkt_openssl_atomic_u64 *value,
                                cpkt_openssl_u64 *result_out,
                                CRYPTO_RWLOCK *lock);
/** C89 adapter for CRYPTO_atomic_or. */
CPKT_OPENSSL_API int cpkt_openssl_CRYPTO_atomic_or(
    cpkt_openssl_atomic_u64 *value, cpkt_openssl_u64 mask,
    cpkt_openssl_u64 *result_out, CRYPTO_RWLOCK *lock);
/** C89 adapter for CRYPTO_atomic_store. */
CPKT_OPENSSL_API int
cpkt_openssl_CRYPTO_atomic_store(cpkt_openssl_atomic_u64 *value,
                                 cpkt_openssl_u64 replacement,
                                 CRYPTO_RWLOCK *lock);
/** C89 adapter for SSL_shutdown_ex. */
CPKT_OPENSSL_API int
cpkt_openssl_SSL_shutdown_ex(SSL *ssl, cpkt_openssl_u64 flags,
                             const cpkt_openssl_ssl_shutdown_args *arguments,
                             size_t arguments_length);
/** C89 adapter for SSL_stream_reset. */
CPKT_OPENSSL_API int cpkt_openssl_SSL_stream_reset(
    SSL *ssl, const cpkt_openssl_ssl_stream_reset_args *arguments,
    size_t arguments_length);
/** C89 adapter for SSL_get_conn_close_info. */
CPKT_OPENSSL_API int cpkt_openssl_SSL_get_conn_close_info(
    SSL *ssl, cpkt_openssl_ssl_conn_close_info *information_out,
    size_t information_length);
/** C89 adapter for BIO_recvmmsg. A zero message count ignores message storage
 * and stride and still invokes the native BIO, including its callbacks and
 * errors. processed_out must be non-NULL, as required by upstream. For a
 * nonempty batch, invalid storage/stride or span overflow returns zero before
 * native dispatch, leaving processed_out unchanged. */
CPKT_OPENSSL_API int
cpkt_openssl_BIO_recvmmsg(BIO *bio, cpkt_openssl_bio_message *messages,
                          size_t message_stride, size_t message_count,
                          cpkt_openssl_u64 flags, size_t *processed_out);
/** C89 adapter for BIO_sendmmsg. Same empty-batch, output-count and nonempty
 * storage-validation contract as cpkt_openssl_BIO_recvmmsg. */
CPKT_OPENSSL_API int
cpkt_openssl_BIO_sendmmsg(BIO *bio, cpkt_openssl_bio_message *messages,
                          size_t message_stride, size_t message_count,
                          cpkt_openssl_u64 flags, size_t *processed_out);
/** Creates a custom method for C89 batch callbacks. Close it after its BIOs. */
CPKT_OPENSSL_API cpkt_openssl_bio_method *
cpkt_openssl_BIO_meth_new(int type, const char *name);
/** Frees a method only after construction finishes, its native BIOs reach
 * their final reference, and no callback runs. Otherwise returns zero without
 * freeing it. An up-ref'd native BIO can outlive its facade BIO. */
CPKT_OPENSSL_API int
cpkt_openssl_BIO_meth_close(cpkt_openssl_bio_method *method);
/** Returns a borrowed native method for C89-native BIO configuration calls. */
CPKT_OPENSSL_API const BIO_METHOD *
cpkt_openssl_BIO_meth_native(const cpkt_openssl_bio_method *method);
/** Registers the send batch callback for a facade-owned method. */
CPKT_OPENSSL_API int
cpkt_openssl_BIO_meth_set_sendmmsg(cpkt_openssl_bio_method *method,
                                   cpkt_openssl_bio_mmsg_callback callback);
/** Returns the send batch callback registered for a facade-owned method. */
CPKT_OPENSSL_API cpkt_openssl_bio_mmsg_callback
cpkt_openssl_BIO_meth_get_sendmmsg(const cpkt_openssl_bio_method *method);
/** Registers the receive batch callback for a facade-owned method. */
CPKT_OPENSSL_API int
cpkt_openssl_BIO_meth_set_recvmmsg(cpkt_openssl_bio_method *method,
                                   cpkt_openssl_bio_mmsg_callback callback);
/** Returns the receive batch callback registered for a facade-owned method. */
CPKT_OPENSSL_API cpkt_openssl_bio_mmsg_callback
cpkt_openssl_BIO_meth_get_recvmmsg(const cpkt_openssl_bio_method *method);
/** Creates a facade BIO. Keep callback context valid until the final native
 * BIO reference is freed, which may follow cpkt_openssl_BIO_close. Native
 * create callbacks may access this method or create another BIO with it;
 * method close is rejected until construction or failure cleanup finishes. */
CPKT_OPENSSL_API cpkt_openssl_bio *
cpkt_openssl_BIO_new(cpkt_openssl_bio_method *method, void *callback_context);
/** Creates a library-context facade BIO with the same callback lifetime. */
CPKT_OPENSSL_API cpkt_openssl_bio *
cpkt_openssl_BIO_new_ex(OSSL_LIB_CTX *library_context,
                        cpkt_openssl_bio_method *method,
                        void *callback_context);
/** Closes one facade BIO. It returns zero during any batch callback. */
CPKT_OPENSSL_API int cpkt_openssl_BIO_close(cpkt_openssl_bio *bio);
/** Returns the borrowed native BIO owned by a facade BIO. BIO_dup_chain on
 * this BIO fails because a copy cannot share its facade ownership. */
CPKT_OPENSSL_API BIO *cpkt_openssl_BIO_native(cpkt_openssl_bio *bio);
/** Sets the legacy callback for a facade BIO. For SENDMMSG/RECVMMSG and their
 * RETURN notifications, argument points to borrowed, read-only C89 mmsg args.
 * Changes to its mutable message records propagate before the method or
 * caller resumes, even when the callback returns failure. */
CPKT_OPENSSL_API void
cpkt_openssl_BIO_set_callback(cpkt_openssl_bio *bio,
                              cpkt_openssl_bio_callback callback);
/** Returns the legacy callback registered for a facade BIO. */
CPKT_OPENSSL_API cpkt_openssl_bio_callback
cpkt_openssl_BIO_get_callback(const cpkt_openssl_bio *bio);
/** Sets the extended callback for a facade BIO. For SENDMMSG/RECVMMSG and their
 * RETURN notifications, argument points to borrowed, read-only C89 mmsg args.
 * Changes to its mutable message records propagate before the method or
 * caller resumes, even when the callback returns failure. */
CPKT_OPENSSL_API void
cpkt_openssl_BIO_set_callback_ex(cpkt_openssl_bio *bio,
                                 cpkt_openssl_bio_callback_ex callback);
/** Returns the extended callback registered for a facade BIO. */
CPKT_OPENSSL_API cpkt_openssl_bio_callback_ex
cpkt_openssl_BIO_get_callback_ex(const cpkt_openssl_bio *bio);
/** C89 adapter for BIO_get_rpoll_descriptor. */
CPKT_OPENSSL_API int cpkt_openssl_BIO_get_rpoll_descriptor(
    BIO *bio, cpkt_openssl_poll_descriptor *descriptor_out);
/** C89 adapter for BIO_get_wpoll_descriptor. */
CPKT_OPENSSL_API int cpkt_openssl_BIO_get_wpoll_descriptor(
    BIO *bio, cpkt_openssl_poll_descriptor *descriptor_out);
/** C89 adapter for SSL_get_rpoll_descriptor. */
CPKT_OPENSSL_API int cpkt_openssl_SSL_get_rpoll_descriptor(
    SSL *ssl, cpkt_openssl_poll_descriptor *descriptor_out);
/** C89 adapter for SSL_get_wpoll_descriptor. */
CPKT_OPENSSL_API int cpkt_openssl_SSL_get_wpoll_descriptor(
    SSL *ssl, cpkt_openssl_poll_descriptor *descriptor_out);
/** C89 replacement for the SSL_as_poll_descriptor inline helper. */
CPKT_OPENSSL_API void cpkt_openssl_SSL_as_poll_descriptor(
    SSL *ssl, cpkt_openssl_poll_descriptor *descriptor_out);
/** C89 adapter for SSL_poll. A zero item count ignores items and item_stride;
 * upstream waits for a supplied finite timeout, or returns immediately for
 * NULL timeout, and writes zero to an optional result_count_out. Nonempty
 * arrays require valid storage and a stride at least sizeof(*items); stride
 * span overflow is rejected. Facade validation failures return zero before
 * native dispatch and leave result_count_out unchanged. Native errors and
 * returned readiness events are preserved. */
CPKT_OPENSSL_API int
cpkt_openssl_SSL_poll(cpkt_openssl_ssl_poll_item *items, size_t item_count,
                      size_t item_stride, const struct timeval *timeout,
                      cpkt_openssl_u64 flags, size_t *result_count_out);

#endif
