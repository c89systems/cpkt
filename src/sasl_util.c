#include <cpkt/sasl.h>

#include <limits.h>
#include <stdlib.h>

#include "sasl_private.h"
#include <sasl/prop.h>
#include <sasl/sasl.h>
#include <sasl/saslutil.h>

/** Release metadata views owned by one facade property wrapper. */
static void cpkt_sasl_property_discard_views(cpkt_sasl_property_context *ctx) {
  if (ctx != NULL) {
    free(ctx->views);
    ctx->views = NULL;
    ctx->view_count = 0;
    ctx->view_capacity = 0;
  }
}

/** C89 facade contract for cpkt_sasl_property_discard_borrowed_views; see the
 * public header for ownership and callback lifetime. */
void cpkt_sasl_property_discard_borrowed_views(
    cpkt_sasl_property_context *context) {
  cpkt_sasl_property_discard_views(context);
}

/** Refresh borrowed native metadata, retaining storage while it still fits. */
static cpkt_sasl_property_value *
cpkt_sasl_property_refresh_views(cpkt_sasl_property_context *ctx,
                                 const struct propval *native, size_t count) {
  cpkt_sasl_property_value *views;
  size_t i;
  if (count == 0) {
    ctx->view_count = 0;
    return NULL;
  }
  if (count > ((size_t)-1) / sizeof(*views)) {
    cpkt_sasl_property_discard_views(ctx);
    return NULL;
  }
  if (count > ctx->view_capacity) {
    views = (cpkt_sasl_property_value *)calloc(count, sizeof(*views));
    if (views == NULL) {
      cpkt_sasl_property_discard_views(ctx);
      return NULL;
    }
    free(ctx->views);
    ctx->views = views;
    ctx->view_capacity = count;
  }
  views = ctx->views;
  for (i = 0; i < count; ++i) {
    views[i].name = native[i].name;
    views[i].values = native[i].values;
    views[i].value_count = native[i].nvalues;
    views[i].total_value_bytes = native[i].valsize;
  }
  ctx->view_count = count;
  return views;
}

/** C89 facade contract for cpkt_sasl_set_allocators; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_set_allocators(cpkt_sasl_malloc_callback malloc_callback,
                              cpkt_sasl_calloc_callback calloc_callback,
                              cpkt_sasl_realloc_callback realloc_callback,
                              cpkt_sasl_free_callback free_callback) {
  sasl_set_alloc(malloc_callback, calloc_callback, realloc_callback,
                 free_callback);
}

/** C89 facade contract for cpkt_sasl_set_mutexes; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_set_mutexes(cpkt_sasl_mutex_alloc_callback allocate,
                           cpkt_sasl_mutex_lock_callback lock,
                           cpkt_sasl_mutex_unlock_callback unlock,
                           cpkt_sasl_mutex_free_callback release) {
  sasl_set_mutex(allocate, lock, unlock, release);
}

/** C89 facade contract for cpkt_sasl_property_new; see the public header for
 * ownership and callback lifetime. */
cpkt_sasl_property_context *cpkt_sasl_property_new(unsigned long estimate) {
  cpkt_sasl_property_context *ctx;
  if (estimate > UINT_MAX)
    return NULL;
  ctx = (cpkt_sasl_property_context *)calloc(1, sizeof(*ctx));
  if (ctx == NULL)
    return NULL;
  ctx->native = prop_new((unsigned)estimate);
  if (ctx->native == NULL) {
    free(ctx);
    return NULL;
  }
  return ctx;
}

/** C89 facade contract for cpkt_sasl_property_duplicate; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_property_duplicate(cpkt_sasl_property_context *source,
                                 cpkt_sasl_property_context **copy_out) {
  cpkt_sasl_property_context *copy;
  int status;
  if (copy_out == NULL || source == NULL)
    return SASL_BADPARAM;
  *copy_out = NULL;
  copy = (cpkt_sasl_property_context *)calloc(1, sizeof(*copy));
  if (copy == NULL)
    return SASL_NOMEM;
  status = prop_dup(source->native, &copy->native);
  if (status != SASL_OK) {
    free(copy);
    return status;
  }
  *copy_out = copy;
  return SASL_OK;
}

/** C89 facade contract for cpkt_sasl_property_request; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_property_request(cpkt_sasl_property_context *context,
                               const char *const *names) {
  if (context == NULL || names == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_property_discard_views(context);
  return prop_request(context->native, (const char **)names);
}

const cpkt_sasl_property_value *
cpkt_sasl_property_get(cpkt_sasl_property_context *context, size_t *count_out) {
  const struct propval *native;
  cpkt_sasl_property_value *views;
  size_t count = 0;
  if (count_out != NULL)
    *count_out = 0;
  if (context == NULL)
    return NULL;
  native = prop_get(context->native);
  if (native == NULL) {
    cpkt_sasl_property_discard_views(context);
    return NULL;
  }
  while (native[count].name != NULL)
    ++count;
  views = cpkt_sasl_property_refresh_views(context, native, count);
  if (count_out != NULL && (views != NULL || count == 0))
    *count_out = count;
  return views;
}

/** C89 facade contract for cpkt_sasl_property_getnames; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_property_getnames(cpkt_sasl_property_context *context,
                                const char *const *names,
                                cpkt_sasl_property_value *values_out,
                                size_t capacity, size_t *count_out) {
  struct propval *native;
  size_t count = 0, i;
  int status;
  if (count_out != NULL)
    *count_out = 0;
  if (context == NULL || names == NULL)
    return SASL_BADPARAM;
  while (names[count] != NULL) {
    if (count == ((size_t)-1) / sizeof(*native))
      return SASL_BADPARAM;
    ++count;
  }
  if (count == 0)
    return 0;
  if (values_out == NULL || capacity < count)
    return SASL_BUFOVER;
  native = (struct propval *)calloc(count, sizeof(*native));
  if (native == NULL)
    return SASL_NOMEM;
  status = prop_getnames(context->native, (const char **)names, native);
  if (status >= 0) {
    for (i = 0; i < count; ++i) {
      values_out[i].name = native[i].name;
      values_out[i].values = native[i].values;
      values_out[i].value_count = native[i].nvalues;
      values_out[i].total_value_bytes = native[i].valsize;
    }
    if (count_out != NULL)
      *count_out = count;
  }
  free(native);
  return status;
}

/** C89 facade contract for cpkt_sasl_property_format; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_property_format(cpkt_sasl_property_context *context,
                              const char *separator, int separator_length,
                              char *output, unsigned long capacity,
                              unsigned long *length_out) {
  unsigned native_length = 0;
  int status;
  if (length_out != NULL)
    *length_out = 0;
  if (context == NULL || capacity > UINT_MAX)
    return SASL_BADPARAM;
  status = prop_format(context->native, separator, separator_length, output,
                       (unsigned)capacity, &native_length);
  if (length_out != NULL)
    *length_out = native_length;
  return status;
}

/** C89 facade contract for cpkt_sasl_property_set; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_property_set(cpkt_sasl_property_context *context,
                           const char *name, const char *value,
                           int value_length) {
  if (context == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_property_discard_views(context);
  return prop_set(context->native, name, value, value_length);
}

/** C89 facade contract for cpkt_sasl_property_set_values; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_property_set_values(cpkt_sasl_property_context *context,
                                  const char *name, const char *const *values) {
  if (context == NULL)
    return SASL_BADPARAM;
  cpkt_sasl_property_discard_views(context);
  return prop_setvals(context->native, name, (const char **)values);
}

/** C89 facade contract for cpkt_sasl_property_clear; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_property_clear(cpkt_sasl_property_context *context,
                              int clear_requests) {
  if (context != NULL) {
    cpkt_sasl_property_discard_views(context);
    prop_clear(context->native, clear_requests);
  }
}

/** C89 facade contract for cpkt_sasl_property_erase; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_property_erase(cpkt_sasl_property_context *context,
                              const char *name) {
  if (context != NULL) {
    cpkt_sasl_property_discard_views(context);
    prop_erase(context->native, name);
  }
}

/** C89 facade contract for cpkt_sasl_property_dispose; see the public header
 * for ownership and callback lifetime. */
void cpkt_sasl_property_dispose(cpkt_sasl_property_context **context) {
  if (context != NULL && *context != NULL) {
    cpkt_sasl_property_discard_views(*context);
    if (!(*context)->borrowed) {
      prop_dispose(&(*context)->native);
      free(*context);
    }
    *context = NULL;
  }
}

static int cpkt_sasl_base64(int encode, const char *input,
                            unsigned long input_length, char *output,
                            unsigned long capacity, unsigned long *length_out) {
  unsigned native_length = 0;
  int status;
  if (length_out != NULL)
    *length_out = 0;
  if (input_length > UINT_MAX || capacity > UINT_MAX)
    return SASL_BADPARAM;
  if (encode)
    status = sasl_encode64(input, (unsigned)input_length, output,
                           (unsigned)capacity, &native_length);
  else
    status = sasl_decode64(input, (unsigned)input_length, output,
                           (unsigned)capacity, &native_length);
  if (length_out != NULL)
    *length_out = native_length;
  return status;
}

/** C89 facade contract for cpkt_sasl_base64_encode; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_base64_encode(const char *input, unsigned long input_length,
                            char *output, unsigned long capacity,
                            unsigned long *length_out) {
  return cpkt_sasl_base64(1, input, input_length, output, capacity, length_out);
}
/** C89 facade contract for cpkt_sasl_base64_decode; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_base64_decode(const char *input, unsigned long input_length,
                            char *output, unsigned long capacity,
                            unsigned long *length_out) {
  return cpkt_sasl_base64(0, input, input_length, output, capacity, length_out);
}
/** C89 facade contract for cpkt_sasl_utf8_verify; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_utf8_verify(const char *input, unsigned long length) {
  return length > UINT_MAX ? SASL_BADPARAM
                           : sasl_utf8verify(input, (unsigned)length);
}
/** C89 facade contract for cpkt_sasl_erase_buffer; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_erase_buffer(char *buffer, unsigned long length) {
  /* The native API cannot represent a larger region on 32-bit unsigned
   * targets. Process it in bounded chunks without losing any bytes. */
  while (length != 0) {
    unsigned chunk = length > UINT_MAX ? UINT_MAX : (unsigned)length;
    sasl_erasebuffer(buffer, chunk);
    buffer += chunk;
    length -= chunk;
  }
}
/** C89 facade contract for cpkt_sasl_lowercase; see the public header for
 * ownership and callback lifetime. */
char *cpkt_sasl_lowercase(char *text) { return sasl_strlower(text); }
/** C89 facade contract for cpkt_sasl_config_initialize; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_config_initialize(const char *file_name) {
  return sasl_config_init(file_name);
}
/** C89 facade contract for cpkt_sasl_config_finish; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_config_finish(void) { sasl_config_done(); }

/** C89 facade contract for cpkt_sasl_random_new; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_random_new(cpkt_sasl_random **out) {
  sasl_rand_t *native = NULL;
  int status;
  if (out == NULL)
    return SASL_BADPARAM;
  *out = NULL;
  status = sasl_randcreate(&native);
  if (status == SASL_OK)
    *out = (cpkt_sasl_random *)native;
  return status;
}
/** C89 facade contract for cpkt_sasl_random_free; see the public header for
 * ownership and callback lifetime. */
void cpkt_sasl_random_free(cpkt_sasl_random **random) {
  sasl_rand_t *native;
  if (random == NULL)
    return;
  native = (sasl_rand_t *)*random;
  sasl_randfree(&native);
  *random = (cpkt_sasl_random *)native;
}
/** C89 facade contract for cpkt_sasl_random_seed; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_random_seed(cpkt_sasl_random *random, const char *bytes,
                          unsigned long length) {
  if (random == NULL || length > UINT_MAX)
    return SASL_BADPARAM;
  sasl_randseed((sasl_rand_t *)random, bytes, (unsigned)length);
  return SASL_OK;
}
/** C89 facade contract for cpkt_sasl_random_fill; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_random_fill(cpkt_sasl_random *random, char *bytes,
                          unsigned long length) {
  if (random == NULL || length > UINT_MAX)
    return SASL_BADPARAM;
  sasl_rand((sasl_rand_t *)random, bytes, (unsigned)length);
  return SASL_OK;
}
/** C89 facade contract for cpkt_sasl_random_churn; see the public header for
 * ownership and callback lifetime. */
int cpkt_sasl_random_churn(cpkt_sasl_random *random, const char *bytes,
                           unsigned long length) {
  if (random == NULL || length > UINT_MAX)
    return SASL_BADPARAM;
  sasl_churn((sasl_rand_t *)random, bytes, (unsigned)length);
  return SASL_OK;
}
