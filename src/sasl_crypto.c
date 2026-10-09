#include <cpkt/sasl_plugin.h>

#include <limits.h>
#include <string.h>

#include <sasl/sasl.h>
#include <sasl/saslplug.h>

typedef char
    cpkt_sasl_crypto_uint4_32_bits[(sizeof(UINT4) * CHAR_BIT == 32) ? 1 : -1];
typedef char cpkt_sasl_crypto_public_word_32_bits
    [(sizeof(unsigned int) * CHAR_BIT == 32) ? 1 : -1];

static void
cpkt_sasl_crypto_md5_to_native(MD5_CTX *native,
                               const cpkt_sasl_md5_context *public_ctx) {
  size_t i;
  for (i = 0; i < 4; ++i)
    native->state[i] = public_ctx->state[i];
  for (i = 0; i < 2; ++i)
    native->count[i] = public_ctx->count[i];
  memcpy(native->buffer, public_ctx->buffer, sizeof(native->buffer));
}

static void cpkt_sasl_crypto_md5_from_native(cpkt_sasl_md5_context *public_ctx,
                                             const MD5_CTX *native) {
  size_t i;
  for (i = 0; i < 4; ++i)
    public_ctx->state[i] = native->state[i];
  for (i = 0; i < 2; ++i)
    public_ctx->count[i] = native->count[i];
  memcpy(public_ctx->buffer, native->buffer, sizeof(public_ctx->buffer));
}

static void
cpkt_sasl_crypto_hmac_to_native(HMAC_MD5_CTX *native,
                                const cpkt_sasl_hmac_md5_context *public_ctx) {
  cpkt_sasl_crypto_md5_to_native(&native->ictx, &public_ctx->inner);
  cpkt_sasl_crypto_md5_to_native(&native->octx, &public_ctx->outer);
}

static void
cpkt_sasl_crypto_hmac_from_native(cpkt_sasl_hmac_md5_context *public_ctx,
                                  const HMAC_MD5_CTX *native) {
  cpkt_sasl_crypto_md5_from_native(&public_ctx->inner, &native->ictx);
  cpkt_sasl_crypto_md5_from_native(&public_ctx->outer, &native->octx);
}

/** C89 facade contract for cpkt_sasl_md5_initialize; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_md5_initialize(cpkt_sasl_md5_context *context) {
  MD5_CTX native;
  if (context == NULL)
    return;
  memset(&native, 0, sizeof(native));
  _sasl_MD5Init(&native);
  cpkt_sasl_crypto_md5_from_native(context, &native);
}

/** C89 facade contract for cpkt_sasl_md5_update; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_md5_update(cpkt_sasl_md5_context *context,
                         const unsigned char *bytes, unsigned long length) {
  MD5_CTX native;
  if (context == NULL || (bytes == NULL && length != 0) || length > UINT_MAX)
    return SASL_BADPARAM;
  cpkt_sasl_crypto_md5_to_native(&native, context);
  _sasl_MD5Update(&native, bytes, (unsigned)length);
  cpkt_sasl_crypto_md5_from_native(context, &native);
  return SASL_OK;
}

/** C89 facade contract for cpkt_sasl_md5_final; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_md5_final(unsigned char digest[16],
                         cpkt_sasl_md5_context *context) {
  MD5_CTX native;
  if (digest == NULL || context == NULL)
    return;
  cpkt_sasl_crypto_md5_to_native(&native, context);
  _sasl_MD5Final(digest, &native);
  cpkt_sasl_crypto_md5_from_native(context, &native);
}

/** C89 facade contract for cpkt_sasl_hmac_md5; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_hmac_md5(const unsigned char *text, int text_length,
                        const unsigned char *key, int key_length,
                        unsigned char digest[16]) {
  if (text_length < 0 || key_length < 0 || digest == NULL ||
      (text == NULL && text_length != 0) || (key == NULL && key_length != 0))
    return;
  _sasl_hmac_md5(text, text_length, key, key_length, digest);
}

/** C89 facade contract for cpkt_sasl_hmac_md5_initialize; see the public header
 * for ownership and callback lifetime. */
void cpkt_sasl_hmac_md5_initialize(cpkt_sasl_hmac_md5_context *context,
                                   const unsigned char *key, int key_length) {
  HMAC_MD5_CTX native;
  if (context == NULL || key_length < 0 || (key == NULL && key_length != 0))
    return;
  memset(&native, 0, sizeof(native));
  _sasl_hmac_md5_init(&native, key, key_length);
  cpkt_sasl_crypto_hmac_from_native(context, &native);
}

/** C89 facade contract for cpkt_sasl_hmac_md5_update; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_hmac_md5_update(cpkt_sasl_hmac_md5_context *context,
                              const unsigned char *bytes,
                              unsigned long length) {
  HMAC_MD5_CTX native;
  if (context == NULL || (bytes == NULL && length != 0) || length > UINT_MAX)
    return SASL_BADPARAM;
  cpkt_sasl_crypto_hmac_to_native(&native, context);
  _sasl_hmac_md5_update(&native, bytes, (unsigned)length);
  cpkt_sasl_crypto_hmac_from_native(context, &native);
  return SASL_OK;
}

/** C89 facade contract for cpkt_sasl_hmac_md5_final; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_hmac_md5_final(unsigned char digest[16],
                              cpkt_sasl_hmac_md5_context *context) {
  HMAC_MD5_CTX native;
  if (digest == NULL || context == NULL)
    return;
  cpkt_sasl_crypto_hmac_to_native(&native, context);
  _sasl_hmac_md5_final(digest, &native);
  cpkt_sasl_crypto_hmac_from_native(context, &native);
}

/** C89 facade contract for cpkt_sasl_hmac_md5_precalculate; see the public
 * header for ownership and callback lifetime. */
void cpkt_sasl_hmac_md5_precalculate(cpkt_sasl_hmac_md5_state *state,
                                     const unsigned char *key, int key_length) {
  HMAC_MD5_STATE native;
  size_t i;
  if (state == NULL || key_length < 0 || (key == NULL && key_length != 0))
    return;
  _sasl_hmac_md5_precalc(&native, key, key_length);
  for (i = 0; i < 4; ++i) {
    state->inner[i] = native.istate[i];
    state->outer[i] = native.ostate[i];
  }
}

/** C89 facade contract for cpkt_sasl_hmac_md5_import; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_hmac_md5_import(cpkt_sasl_hmac_md5_context *context,
                               const cpkt_sasl_hmac_md5_state *state) {
  HMAC_MD5_CTX native;
  HMAC_MD5_STATE native_state;
  size_t i;
  if (context == NULL || state == NULL)
    return;
  memset(&native, 0, sizeof(native));
  for (i = 0; i < 4; ++i) {
    native_state.istate[i] = state->inner[i];
    native_state.ostate[i] = state->outer[i];
  }
  _sasl_hmac_md5_import(&native, &native_state);
  cpkt_sasl_crypto_hmac_from_native(context, &native);
}
