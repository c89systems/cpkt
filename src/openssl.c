#include <cpkt/openssl.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bioerr.h>
#include <openssl/err.h>

typedef char cpkt_openssl_u64_is_eight_bytes[sizeof(uint64_t) == 8 ? 1 : -1];
typedef char cpkt_openssl_i64_is_eight_bytes[sizeof(int64_t) == 8 ? 1 : -1];
typedef char cpkt_openssl_octet_is_eight_bits[CHAR_BIT == 8 ? 1 : -1];
typedef char
    cpkt_openssl_public_u64_is_eight_bytes[sizeof(cpkt_openssl_u64) == 8 ? 1
                                                                         : -1];
typedef char
    cpkt_openssl_public_i64_is_eight_bytes[sizeof(cpkt_openssl_i64) == 8 ? 1
                                                                         : -1];
typedef char cpkt_openssl_uintptr_fits_public_value
    [sizeof(uintptr_t) <= sizeof(cpkt_openssl_u64) ? 1 : -1];

struct cpkt_openssl_param_i64 {
  OSSL_PARAM native;
  int64_t value;
};

struct cpkt_openssl_param_u64 {
  OSSL_PARAM native;
  uint64_t value;
};

struct cpkt_openssl_sha512_context {
  SHA512_CTX native;
};

struct cpkt_openssl_atomic_u64 {
  uint64_t native;
};

struct cpkt_openssl_bio_method {
  BIO_METHOD *native;
  CRYPTO_RWLOCK *lock;
  cpkt_openssl_bio_mmsg_callback send_callback;
  cpkt_openssl_bio_mmsg_callback receive_callback;
  size_t active_bios;
  size_t active_callbacks;
  int closing;
};

struct cpkt_openssl_bio {
  BIO *native;
  cpkt_openssl_bio_method *method;
  void *callback_context;
  cpkt_openssl_bio_callback callback;
  cpkt_openssl_bio_callback_ex callback_ex;
};

static CRYPTO_ONCE cpkt_openssl_bio_ex_data_once = CRYPTO_ONCE_STATIC_INIT;
static int cpkt_openssl_bio_ex_data_index = -1;

/* Release the same method reservation after an unfinished constructor or
 * after the last native BIO reference releases its ex-data owner. */
static void cpkt_openssl_bio_release_shell(cpkt_openssl_bio *bio) {
  cpkt_openssl_bio_method *method;
  if (bio == NULL)
    return;
  method = bio->method;
  if (CRYPTO_THREAD_write_lock(method->lock)) {
    --method->active_bios;
    CRYPTO_THREAD_unlock(method->lock);
  }
  free(bio);
}

static void cpkt_openssl_bio_ex_data_free(void *parent, void *pointer,
                                          CRYPTO_EX_DATA *data, int index,
                                          long argument, void *context) {
  (void)parent;
  (void)data;
  (void)index;
  (void)argument;
  (void)context;
  cpkt_openssl_bio_release_shell((cpkt_openssl_bio *)pointer);
}

static int cpkt_openssl_bio_ex_data_dup(CRYPTO_EX_DATA *to,
                                        const CRYPTO_EX_DATA *from,
                                        void **pointer, int index,
                                        long argument, void *context) {
  (void)to;
  (void)from;
  (void)index;
  (void)argument;
  (void)context;
  if (pointer == NULL || *pointer == NULL)
    return 1;
  /* The facade shell owns the native BIO and its method pin. A shallow copy
   * would give both BIOs the same owner and release it twice. */
  ERR_raise(ERR_LIB_BIO, BIO_R_UNSUPPORTED_METHOD);
  return 0;
}

static void cpkt_openssl_bio_ex_data_initialize(void) {
  cpkt_openssl_bio_ex_data_index =
      BIO_get_ex_new_index(0L, NULL, NULL, cpkt_openssl_bio_ex_data_dup,
                           cpkt_openssl_bio_ex_data_free);
}

static uint64_t cpkt_openssl_native_u64(cpkt_openssl_u64 value) {
  uint64_t native;

  memcpy(&native, value.bytes, sizeof(native));
  return native;
}

static cpkt_openssl_u64 cpkt_openssl_public_u64(uint64_t value) {
  cpkt_openssl_u64 public_value;

  memcpy(public_value.bytes, &value, sizeof(value));
  return public_value;
}

static int64_t cpkt_openssl_native_i64(cpkt_openssl_i64 value) {
  uint64_t bits;
  int64_t native;

  memcpy(&bits, value.bytes, sizeof(bits));
  memcpy(&native, &bits, sizeof(native));
  return native;
}

static cpkt_openssl_i64 cpkt_openssl_public_i64(int64_t value) {
  uint64_t bits;
  cpkt_openssl_u64 public_bits;
  cpkt_openssl_i64 public_value;

  memcpy(&bits, &value, sizeof(bits));
  public_bits = cpkt_openssl_public_u64(bits);
  memcpy(public_value.bytes, public_bits.bytes, sizeof(public_value.bytes));
  return public_value;
}

static size_t cpkt_openssl_native_length(size_t public_length,
                                         size_t public_size,
                                         size_t native_size) {
  return public_length == public_size ? native_size : public_length;
}

static int cpkt_openssl_native_uintptr(cpkt_openssl_u64 value,
                                       uintptr_t *native_out) {
  uint64_t native_value;

  if (native_out == NULL || (sizeof(uintptr_t) < sizeof(native_value) &&
                             cpkt_openssl_u64_high_word(value) != 0UL)) {
    return 0;
  }
  native_value = cpkt_openssl_native_u64(value);
  *native_out = (uintptr_t)native_value;
  return 1;
}

static int cpkt_openssl_copy_poll_descriptor_to_native(
    BIO_POLL_DESCRIPTOR *native_descriptor,
    const cpkt_openssl_poll_descriptor *public_descriptor) {
  uintptr_t custom_uintptr;

  if (native_descriptor == NULL || public_descriptor == NULL ||
      (public_descriptor->type & ~0xffffffffUL) != 0UL) {
    return 0;
  }
  memset(native_descriptor, 0, sizeof(*native_descriptor));
  native_descriptor->type = (uint32_t)public_descriptor->type;
  switch (public_descriptor->value_kind) {
  case CPKT_OPENSSL_POLL_VALUE_NONE:
    return 1;
  case CPKT_OPENSSL_POLL_VALUE_FD:
    native_descriptor->value.fd = public_descriptor->file_descriptor;
    return 1;
  case CPKT_OPENSSL_POLL_VALUE_CUSTOM_POINTER:
    native_descriptor->value.custom = public_descriptor->custom_pointer;
    return 1;
  case CPKT_OPENSSL_POLL_VALUE_CUSTOM_UINTPTR:
    if (!cpkt_openssl_native_uintptr(public_descriptor->custom_uintptr,
                                     &custom_uintptr)) {
      return 0;
    }
    native_descriptor->value.custom_ui = custom_uintptr;
    return 1;
  case CPKT_OPENSSL_POLL_VALUE_SSL:
    native_descriptor->value.ssl = public_descriptor->ssl;
    return 1;
  default:
    return 0;
  }
}

static void cpkt_openssl_copy_poll_descriptor_from_native(
    cpkt_openssl_poll_descriptor *public_descriptor,
    const BIO_POLL_DESCRIPTOR *native_descriptor) {
  memset(public_descriptor, 0, sizeof(*public_descriptor));
  public_descriptor->type = (unsigned long)native_descriptor->type;
  if (native_descriptor->type == BIO_POLL_DESCRIPTOR_TYPE_SOCK_FD) {
    public_descriptor->value_kind = CPKT_OPENSSL_POLL_VALUE_FD;
    public_descriptor->file_descriptor = native_descriptor->value.fd;
  } else if (native_descriptor->type == BIO_POLL_DESCRIPTOR_TYPE_SSL) {
    public_descriptor->value_kind = CPKT_OPENSSL_POLL_VALUE_SSL;
    public_descriptor->ssl = native_descriptor->value.ssl;
  } else if (native_descriptor->type != BIO_POLL_DESCRIPTOR_TYPE_NONE) {
    public_descriptor->value_kind = CPKT_OPENSSL_POLL_VALUE_CUSTOM_POINTER;
    public_descriptor->custom_pointer = native_descriptor->value.custom;
  }
}

/* Bound both the native allocation and the caller's strided record span. */
static int cpkt_openssl_record_span_valid(size_t count, size_t stride,
                                          size_t element_size) {
  return count == 0 || (stride >= element_size &&
                        count - 1 <= (SIZE_MAX - element_size) / stride);
}

static int
cpkt_openssl_copy_poll_items_to_native(SSL_POLL_ITEM *native_items,
                                       const cpkt_openssl_ssl_poll_item *items,
                                       size_t item_stride, size_t item_count) {
  size_t index;

  for (index = 0; index < item_count; ++index) {
    const cpkt_openssl_ssl_poll_item *public_item;

    public_item =
        (const cpkt_openssl_ssl_poll_item *)((const unsigned char *)items +
                                             index * item_stride);
    if (!cpkt_openssl_copy_poll_descriptor_to_native(
            &native_items[index].desc, &public_item->descriptor)) {
      return 0;
    }
    native_items[index].events = cpkt_openssl_native_u64(public_item->events);
    native_items[index].revents =
        cpkt_openssl_native_u64(public_item->returned_events);
  }
  return 1;
}

static void cpkt_openssl_copy_poll_items_from_native(
    cpkt_openssl_ssl_poll_item *items, size_t item_stride,
    const SSL_POLL_ITEM *native_items, size_t item_count) {
  size_t index;

  for (index = 0; index < item_count; ++index) {
    cpkt_openssl_ssl_poll_item *public_item;

    public_item = (cpkt_openssl_ssl_poll_item *)((unsigned char *)items +
                                                 index * item_stride);
    cpkt_openssl_copy_poll_descriptor_from_native(&public_item->descriptor,
                                                  &native_items[index].desc);
    public_item->events = cpkt_openssl_public_u64(native_items[index].events);
    public_item->returned_events =
        cpkt_openssl_public_u64(native_items[index].revents);
  }
}

static void cpkt_openssl_copy_bio_messages_to_native(
    BIO_MSG *native_messages, size_t native_message_stride,
    const cpkt_openssl_bio_message *messages, size_t message_stride,
    size_t message_count) {
  size_t index;

  for (index = 0; index < message_count; ++index) {
    BIO_MSG *native_message;
    const cpkt_openssl_bio_message *public_message;

    native_message = (BIO_MSG *)((unsigned char *)native_messages +
                                 index * native_message_stride);
    public_message =
        (const cpkt_openssl_bio_message *)((const unsigned char *)messages +
                                           index * message_stride);
    native_message->data = public_message->data;
    native_message->data_len = public_message->data_length;
    native_message->peer = public_message->peer;
    native_message->local = public_message->local;
    native_message->flags = cpkt_openssl_native_u64(public_message->flags);
  }
}

static void cpkt_openssl_copy_bio_messages_from_native(
    cpkt_openssl_bio_message *messages, size_t message_stride,
    const BIO_MSG *native_messages, size_t native_message_stride,
    size_t message_count) {
  size_t index;

  for (index = 0; index < message_count; ++index) {
    cpkt_openssl_bio_message *public_message;
    const BIO_MSG *native_message;

    public_message = (cpkt_openssl_bio_message *)((unsigned char *)messages +
                                                  index * message_stride);
    native_message = (const BIO_MSG *)((const unsigned char *)native_messages +
                                       index * native_message_stride);
    public_message->data = native_message->data;
    public_message->data_length = native_message->data_len;
    public_message->peer = native_message->peer;
    public_message->local = native_message->local;
    public_message->flags = cpkt_openssl_public_u64(native_message->flags);
  }
}

static int
cpkt_openssl_bio_mmsg_callback_invoke(BIO *bio, BIO_MSG *native_messages,
                                      size_t native_stride,
                                      size_t message_count, uint64_t flags,
                                      size_t *processed_out, int is_receive) {
  cpkt_openssl_bio *facade_bio;
  cpkt_openssl_bio_method *method;
  cpkt_openssl_bio_mmsg_callback callback;
  cpkt_openssl_bio_message *public_messages;
  int result;

  if (bio == NULL ||
      (message_count != 0 &&
       (native_messages == NULL ||
        !cpkt_openssl_record_span_valid(message_count, native_stride,
                                        sizeof(*native_messages)) ||
        message_count > SIZE_MAX / sizeof(*public_messages)))) {
    return 0;
  }
  facade_bio = (cpkt_openssl_bio *)BIO_get_callback_arg(bio);
  if (facade_bio == NULL || facade_bio->method == NULL) {
    return 0;
  }
  method = facade_bio->method;
  if (!CRYPTO_THREAD_write_lock(method->lock)) {
    return 0;
  }
  if (method->closing) {
    CRYPTO_THREAD_unlock(method->lock);
    return 0;
  }
  callback = is_receive ? method->receive_callback : method->send_callback;
  if (callback == NULL) {
    CRYPTO_THREAD_unlock(method->lock);
    return 0;
  }
  ++method->active_callbacks;
  CRYPTO_THREAD_unlock(method->lock);
  public_messages = (cpkt_openssl_bio_message *)calloc(
      message_count, sizeof(*public_messages));
  if (public_messages == NULL && message_count != 0) {
    result = 0;
  } else {
    cpkt_openssl_copy_bio_messages_from_native(
        public_messages, sizeof(*public_messages), native_messages,
        native_stride, message_count);
    result = callback(facade_bio->callback_context, bio, public_messages,
                      sizeof(*public_messages), message_count,
                      cpkt_openssl_public_u64(flags), processed_out);
    cpkt_openssl_copy_bio_messages_to_native(
        native_messages, native_stride, public_messages,
        sizeof(*public_messages), message_count);
    free(public_messages);
  }
  CRYPTO_THREAD_write_lock(method->lock);
  --method->active_callbacks;
  CRYPTO_THREAD_unlock(method->lock);
  return result;
}

static int cpkt_openssl_bio_callback_mmsg_args(
    int operation, const char *argument,
    cpkt_openssl_bio_mmsg_callback_args *public_args,
    cpkt_openssl_bio_message **messages_out) {
  const BIO_MMSG_CB_ARGS *native_args;
  cpkt_openssl_bio_message *public_messages;

  *messages_out = NULL;
  operation &= ~BIO_CB_RETURN;
  if (operation != BIO_CB_SENDMMSG && operation != BIO_CB_RECVMMSG) {
    return 0;
  }
  if (argument == NULL || public_args == NULL) {
    return -1;
  }
  native_args = (const BIO_MMSG_CB_ARGS *)(const void *)argument;
  if (native_args->num_msg != 0 &&
      (native_args->msg == NULL ||
       !cpkt_openssl_record_span_valid(native_args->num_msg,
                                       native_args->stride,
                                       sizeof(*native_args->msg)) ||
       native_args->num_msg > SIZE_MAX / sizeof(*public_messages))) {
    return -1;
  }
  public_messages = (cpkt_openssl_bio_message *)calloc(
      native_args->num_msg, sizeof(*public_messages));
  if (public_messages == NULL && native_args->num_msg != 0) {
    return -1;
  }
  cpkt_openssl_copy_bio_messages_from_native(
      public_messages, sizeof(*public_messages), native_args->msg,
      native_args->stride, native_args->num_msg);
  public_args->messages = public_messages;
  public_args->message_stride = sizeof(*public_messages);
  public_args->message_count = native_args->num_msg;
  public_args->flags = cpkt_openssl_public_u64(native_args->flags);
  public_args->processed_out = native_args->msgs_processed;
  *messages_out = public_messages;
  return 1;
}

/* The argument descriptor is borrowed and read-only, but its message records
 * are mutable in native BIO callbacks, before and after the batch operation. */
static void cpkt_openssl_bio_callback_mmsg_copy_back(
    const char *argument, const cpkt_openssl_bio_message *messages) {
  const BIO_MMSG_CB_ARGS *native_args;
  native_args = (const BIO_MMSG_CB_ARGS *)(const void *)argument;
  cpkt_openssl_copy_bio_messages_to_native(
      native_args->msg, native_args->stride, messages, sizeof(*messages),
      native_args->num_msg);
}

static int
cpkt_openssl_bio_callback_enter(cpkt_openssl_bio *facade_bio,
                                cpkt_openssl_bio_method **method_out) {
  cpkt_openssl_bio_method *method;

  if (facade_bio == NULL || facade_bio->method == NULL) {
    return 0;
  }
  method = facade_bio->method;
  if (!CRYPTO_THREAD_write_lock(method->lock) || method->closing) {
    return 0;
  }
  ++method->active_callbacks;
  CRYPTO_THREAD_unlock(method->lock);
  *method_out = method;
  return 1;
}

static void cpkt_openssl_bio_callback_leave(cpkt_openssl_bio_method *method) {
  CRYPTO_THREAD_write_lock(method->lock);
  --method->active_callbacks;
  CRYPTO_THREAD_unlock(method->lock);
}

static long cpkt_openssl_bio_callback_trampoline(BIO *bio, int operation,
                                                 const char *argument,
                                                 int argument_integer,
                                                 long argument_long,
                                                 long result) {
  cpkt_openssl_bio *facade_bio;
  cpkt_openssl_bio_method *method;
  cpkt_openssl_bio_callback callback;
  cpkt_openssl_bio_mmsg_callback_args public_args;
  cpkt_openssl_bio_message *public_messages;
  const void *public_argument;
  int converted;
  long callback_result;

  facade_bio = (cpkt_openssl_bio *)BIO_get_callback_arg(bio);
  if (facade_bio != NULL && facade_bio->native != bio)
    return 1;
  if (!cpkt_openssl_bio_callback_enter(facade_bio, &method)) {
    return 0;
  }
  if (!CRYPTO_THREAD_read_lock(method->lock)) {
    cpkt_openssl_bio_callback_leave(method);
    return 0;
  }
  callback = facade_bio->callback;
  CRYPTO_THREAD_unlock(method->lock);
  if (callback == NULL) {
    cpkt_openssl_bio_callback_leave(method);
    return 1;
  }
  public_messages = NULL;
  public_argument = argument;
  converted = cpkt_openssl_bio_callback_mmsg_args(
      operation, argument, &public_args, &public_messages);
  if (converted < 0) {
    cpkt_openssl_bio_callback_leave(method);
    return 0;
  }
  if (converted > 0) {
    public_argument = &public_args;
  }
  callback_result =
      callback(facade_bio->callback_context, bio, operation, public_argument,
               argument_integer, argument_long, result);
  if (converted > 0)
    cpkt_openssl_bio_callback_mmsg_copy_back(argument, public_messages);
  free(public_messages);
  cpkt_openssl_bio_callback_leave(method);
  return callback_result;
}

static long cpkt_openssl_bio_callback_ex_trampoline(
    BIO *bio, int operation, const char *argument, size_t argument_length,
    int argument_integer, long argument_long, int result,
    size_t *processed_out) {
  cpkt_openssl_bio *facade_bio;
  cpkt_openssl_bio_method *method;
  cpkt_openssl_bio_callback_ex callback;
  cpkt_openssl_bio_mmsg_callback_args public_args;
  cpkt_openssl_bio_message *public_messages;
  const void *public_argument;
  int converted;
  long callback_result;

  facade_bio = (cpkt_openssl_bio *)BIO_get_callback_arg(bio);
  if (facade_bio != NULL && facade_bio->native != bio)
    return 1;
  if (!cpkt_openssl_bio_callback_enter(facade_bio, &method)) {
    return 0;
  }
  if (!CRYPTO_THREAD_read_lock(method->lock)) {
    cpkt_openssl_bio_callback_leave(method);
    return 0;
  }
  callback = facade_bio->callback_ex;
  CRYPTO_THREAD_unlock(method->lock);
  if (callback == NULL) {
    cpkt_openssl_bio_callback_leave(method);
    return 1;
  }
  public_messages = NULL;
  public_argument = argument;
  converted = cpkt_openssl_bio_callback_mmsg_args(
      operation, argument, &public_args, &public_messages);
  if (converted < 0) {
    cpkt_openssl_bio_callback_leave(method);
    return 0;
  }
  if (converted > 0) {
    public_argument = &public_args;
  }
  callback_result = callback(facade_bio->callback_context, bio, operation,
                             public_argument, argument_length, argument_integer,
                             argument_long, result, processed_out);
  if (converted > 0)
    cpkt_openssl_bio_callback_mmsg_copy_back(argument, public_messages);
  free(public_messages);
  cpkt_openssl_bio_callback_leave(method);
  return callback_result;
}

static int
cpkt_openssl_bio_sendmmsg_callback(BIO *bio, BIO_MSG *native_messages,
                                   size_t native_stride, size_t message_count,
                                   uint64_t flags, size_t *processed_out) {
  return cpkt_openssl_bio_mmsg_callback_invoke(bio, native_messages,
                                               native_stride, message_count,
                                               flags, processed_out, 0);
}

static int
cpkt_openssl_bio_recvmmsg_callback(BIO *bio, BIO_MSG *native_messages,
                                   size_t native_stride, size_t message_count,
                                   uint64_t flags, size_t *processed_out) {
  return cpkt_openssl_bio_mmsg_callback_invoke(bio, native_messages,
                                               native_stride, message_count,
                                               flags, processed_out, 1);
}

static cpkt_openssl_bio *
cpkt_openssl_bio_new_internal(OSSL_LIB_CTX *library_context,
                              cpkt_openssl_bio_method *method,
                              void *callback_context) {
  cpkt_openssl_bio *facade_bio;
  BIO *native_bio;
  BIO_METHOD *native_method;

  if (!CRYPTO_THREAD_run_once(&cpkt_openssl_bio_ex_data_once,
                              cpkt_openssl_bio_ex_data_initialize) ||
      cpkt_openssl_bio_ex_data_index < 0 || method == NULL ||
      method->lock == NULL || !CRYPTO_THREAD_write_lock(method->lock)) {
    return NULL;
  }
  if (method->closing || method->native == NULL) {
    CRYPTO_THREAD_unlock(method->lock);
    return NULL;
  }
  facade_bio = (cpkt_openssl_bio *)calloc(1U, sizeof(*facade_bio));
  if (facade_bio == NULL) {
    CRYPTO_THREAD_unlock(method->lock);
    return NULL;
  }
  /* Native construction and destruction invoke application callbacks. Pin
   * the method before unlocking so those callbacks can safely reenter its
   * facade, but cannot close it while construction remains in progress. */
  facade_bio->method = method;
  facade_bio->callback_context = callback_context;
  native_method = method->native;
  ++method->active_bios;
  CRYPTO_THREAD_unlock(method->lock);
  native_bio = BIO_new_ex(library_context, native_method);
  if (native_bio == NULL) {
    cpkt_openssl_bio_release_shell(facade_bio);
    return NULL;
  }
  if (!BIO_set_ex_data(native_bio, cpkt_openssl_bio_ex_data_index,
                       facade_bio)) {
    BIO_free(native_bio);
    cpkt_openssl_bio_release_shell(facade_bio);
    return NULL;
  }
  facade_bio->native = native_bio;
  BIO_set_callback_arg(native_bio, (char *)facade_bio);
  return facade_bio;
}

/** Implements the documented public C89 OpenSSL facade operation
 * cpkt_openssl_u64_make. */
cpkt_openssl_u64 cpkt_openssl_u64_make(unsigned long high, unsigned long low) {
  uint64_t native;

  native = (uint64_t)(high & 0xffffffffUL);
  native <<= 32;
  native |= (uint64_t)(low & 0xffffffffUL);
  return cpkt_openssl_public_u64(native);
}

/** Implements the documented public C89 OpenSSL facade operation
 * cpkt_openssl_u64_high_word. */
unsigned long cpkt_openssl_u64_high_word(cpkt_openssl_u64 value) {
  return (unsigned long)(cpkt_openssl_native_u64(value) >> 32);
}

/** Implements the documented public C89 OpenSSL facade operation
 * cpkt_openssl_u64_low_word. */
unsigned long cpkt_openssl_u64_low_word(cpkt_openssl_u64 value) {
  return (unsigned long)(cpkt_openssl_native_u64(value) & 0xffffffffUL);
}

/** Implements the documented public C89 OpenSSL facade operation
 * cpkt_openssl_u64_equal. */
int cpkt_openssl_u64_equal(cpkt_openssl_u64 left, cpkt_openssl_u64 right) {
  return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

/** Implements the documented public C89 OpenSSL facade operation
 * cpkt_openssl_u64_is_zero. */
int cpkt_openssl_u64_is_zero(cpkt_openssl_u64 value) {
  return cpkt_openssl_u64_high_word(value) == 0UL &&
         cpkt_openssl_u64_low_word(value) == 0UL;
}

/** Implements the documented public C89 OpenSSL facade operation
 * cpkt_openssl_i64_make. */
cpkt_openssl_i64 cpkt_openssl_i64_make(unsigned long high, unsigned long low) {
  cpkt_openssl_u64 unsigned_value;
  cpkt_openssl_i64 value;

  unsigned_value = cpkt_openssl_u64_make(high, low);
  memcpy(value.bytes, unsigned_value.bytes, sizeof(value.bytes));
  return value;
}

/** Implements the documented public C89 OpenSSL facade operation
 * cpkt_openssl_i64_high_word. */
unsigned long cpkt_openssl_i64_high_word(cpkt_openssl_i64 value) {
  cpkt_openssl_u64 unsigned_value;

  memcpy(unsigned_value.bytes, value.bytes, sizeof(unsigned_value.bytes));
  return cpkt_openssl_u64_high_word(unsigned_value);
}

/** Implements the documented public C89 OpenSSL facade operation
 * cpkt_openssl_i64_low_word. */
unsigned long cpkt_openssl_i64_low_word(cpkt_openssl_i64 value) {
  cpkt_openssl_u64 unsigned_value;

  memcpy(unsigned_value.bytes, value.bytes, sizeof(unsigned_value.bytes));
  return cpkt_openssl_u64_low_word(unsigned_value);
}

/** Implements the documented public C89 OpenSSL facade operation
 * cpkt_openssl_i64_equal. */
int cpkt_openssl_i64_equal(cpkt_openssl_i64 left, cpkt_openssl_i64 right) {
  return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OPENSSL_init_crypto. */
int cpkt_openssl_OPENSSL_init_crypto(cpkt_openssl_u64 options,
                                     const OPENSSL_INIT_SETTINGS *settings) {
  return OPENSSL_init_crypto(cpkt_openssl_native_u64(options), settings);
}

/** Implements the documented public C89 adapter cpkt_openssl_OPENSSL_init_ssl.
 */
int cpkt_openssl_OPENSSL_init_ssl(cpkt_openssl_u64 options,
                                  const OPENSSL_INIT_SETTINGS *settings) {
  return OPENSSL_init_ssl(cpkt_openssl_native_u64(options), settings);
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_CTX_get_options. */
cpkt_openssl_u64 cpkt_openssl_SSL_CTX_get_options(const SSL_CTX *context) {
  return cpkt_openssl_public_u64(SSL_CTX_get_options(context));
}

/** Implements the documented public C89 adapter cpkt_openssl_SSL_get_options.
 */
cpkt_openssl_u64 cpkt_openssl_SSL_get_options(const SSL *ssl) {
  return cpkt_openssl_public_u64(SSL_get_options(ssl));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_CTX_set_options. */
cpkt_openssl_u64 cpkt_openssl_SSL_CTX_set_options(SSL_CTX *context,
                                                  cpkt_openssl_u64 options) {
  return cpkt_openssl_public_u64(
      SSL_CTX_set_options(context, cpkt_openssl_native_u64(options)));
}

/** Implements the documented public C89 adapter cpkt_openssl_SSL_set_options.
 */
cpkt_openssl_u64 cpkt_openssl_SSL_set_options(SSL *ssl,
                                              cpkt_openssl_u64 options) {
  return cpkt_openssl_public_u64(
      SSL_set_options(ssl, cpkt_openssl_native_u64(options)));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_CTX_clear_options. */
cpkt_openssl_u64 cpkt_openssl_SSL_CTX_clear_options(SSL_CTX *context,
                                                    cpkt_openssl_u64 options) {
  return cpkt_openssl_public_u64(
      SSL_CTX_clear_options(context, cpkt_openssl_native_u64(options)));
}

/** Implements the documented public C89 adapter cpkt_openssl_SSL_clear_options.
 */
cpkt_openssl_u64 cpkt_openssl_SSL_clear_options(SSL *ssl,
                                                cpkt_openssl_u64 options) {
  return cpkt_openssl_public_u64(
      SSL_clear_options(ssl, cpkt_openssl_native_u64(options)));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_get_handshake_rtt. */
int cpkt_openssl_SSL_get_handshake_rtt(const SSL *ssl,
                                       cpkt_openssl_u64 *rtt_out) {
  uint64_t native_rtt;
  int result;

  native_rtt = 0;
  result = SSL_get_handshake_rtt(ssl, rtt_out == NULL ? NULL : &native_rtt);
  if (result != 0 && rtt_out != NULL) {
    *rtt_out = cpkt_openssl_public_u64(native_rtt);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_ASN1_ENUMERATED_get_int64. */
int cpkt_openssl_ASN1_ENUMERATED_get_int64(cpkt_openssl_i64 *value_out,
                                           const ASN1_ENUMERATED *enumerated) {
  int64_t native_value;
  int result;

  native_value = 0;
  result = ASN1_ENUMERATED_get_int64(value_out == NULL ? NULL : &native_value,
                                     enumerated);
  if (result != 0 && value_out != NULL) {
    *value_out = cpkt_openssl_public_i64(native_value);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_ASN1_ENUMERATED_set_int64. */
int cpkt_openssl_ASN1_ENUMERATED_set_int64(ASN1_ENUMERATED *enumerated,
                                           cpkt_openssl_i64 value) {
  return ASN1_ENUMERATED_set_int64(enumerated, cpkt_openssl_native_i64(value));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_ASN1_INTEGER_get_int64. */
int cpkt_openssl_ASN1_INTEGER_get_int64(cpkt_openssl_i64 *value_out,
                                        const ASN1_INTEGER *integer) {
  int64_t native_value;
  int result;

  native_value = 0;
  result =
      ASN1_INTEGER_get_int64(value_out == NULL ? NULL : &native_value, integer);
  if (result != 0 && value_out != NULL) {
    *value_out = cpkt_openssl_public_i64(native_value);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_ASN1_INTEGER_get_uint64. */
int cpkt_openssl_ASN1_INTEGER_get_uint64(cpkt_openssl_u64 *value_out,
                                         const ASN1_INTEGER *integer) {
  uint64_t native_value;
  int result;

  native_value = 0;
  result = ASN1_INTEGER_get_uint64(value_out == NULL ? NULL : &native_value,
                                   integer);
  if (result != 0 && value_out != NULL) {
    *value_out = cpkt_openssl_public_u64(native_value);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_ASN1_INTEGER_set_int64. */
int cpkt_openssl_ASN1_INTEGER_set_int64(ASN1_INTEGER *integer,
                                        cpkt_openssl_i64 value) {
  return ASN1_INTEGER_set_int64(integer, cpkt_openssl_native_i64(value));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_ASN1_INTEGER_set_uint64. */
int cpkt_openssl_ASN1_INTEGER_set_uint64(ASN1_INTEGER *integer,
                                         cpkt_openssl_u64 value) {
  return ASN1_INTEGER_set_uint64(integer, cpkt_openssl_native_u64(value));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_CT_POLICY_EVAL_CTX_get_time. */
cpkt_openssl_u64
cpkt_openssl_CT_POLICY_EVAL_CTX_get_time(const CT_POLICY_EVAL_CTX *context) {
  return cpkt_openssl_public_u64(CT_POLICY_EVAL_CTX_get_time(context));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_CT_POLICY_EVAL_CTX_set_time. */
void cpkt_openssl_CT_POLICY_EVAL_CTX_set_time(CT_POLICY_EVAL_CTX *context,
                                              cpkt_openssl_u64 value) {
  CT_POLICY_EVAL_CTX_set_time(context, cpkt_openssl_native_u64(value));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OSSL_get_max_threads. */
cpkt_openssl_u64
cpkt_openssl_OSSL_get_max_threads(OSSL_LIB_CTX *library_context) {
  return cpkt_openssl_public_u64(OSSL_get_max_threads(library_context));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OSSL_set_max_threads. */
int cpkt_openssl_OSSL_set_max_threads(OSSL_LIB_CTX *library_context,
                                      cpkt_openssl_u64 value) {
  return OSSL_set_max_threads(library_context, cpkt_openssl_native_u64(value));
}

/** Implements the documented public C89 adapter cpkt_openssl_OSSL_sleep. */
void cpkt_openssl_OSSL_sleep(cpkt_openssl_u64 milliseconds) {
  OSSL_sleep(cpkt_openssl_native_u64(milliseconds));
}

/** Implements the documented public C89 adapter cpkt_openssl_SCT_get_timestamp.
 */
cpkt_openssl_u64 cpkt_openssl_SCT_get_timestamp(const SCT *sct) {
  return cpkt_openssl_public_u64(SCT_get_timestamp(sct));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SCT_new_from_base64. */
SCT *cpkt_openssl_SCT_new_from_base64(unsigned char version, const char *log_id,
                                      ct_log_entry_type_t entry_type,
                                      cpkt_openssl_u64 timestamp,
                                      const char *extensions,
                                      const char *signature) {
  return SCT_new_from_base64(version, log_id, entry_type,
                             cpkt_openssl_native_u64(timestamp), extensions,
                             signature);
}

/** Implements the documented public C89 adapter cpkt_openssl_SCT_set_timestamp.
 */
void cpkt_openssl_SCT_set_timestamp(SCT *sct, cpkt_openssl_u64 timestamp) {
  SCT_set_timestamp(sct, cpkt_openssl_native_u64(timestamp));
}

/** Implements the documented public C89 adapter cpkt_openssl_EVP_PBE_scrypt. */
int cpkt_openssl_EVP_PBE_scrypt(const char *password, size_t password_length,
                                const unsigned char *salt, size_t salt_length,
                                cpkt_openssl_u64 work_factor,
                                cpkt_openssl_u64 block_size,
                                cpkt_openssl_u64 parallelization,
                                cpkt_openssl_u64 maximum_memory,
                                unsigned char *key, size_t key_length) {
  return EVP_PBE_scrypt(
      password, password_length, salt, salt_length,
      cpkt_openssl_native_u64(work_factor), cpkt_openssl_native_u64(block_size),
      cpkt_openssl_native_u64(parallelization),
      cpkt_openssl_native_u64(maximum_memory), key, key_length);
}

/** Implements the documented public C89 adapter cpkt_openssl_EVP_PBE_scrypt_ex.
 */
int cpkt_openssl_EVP_PBE_scrypt_ex(
    const char *password, size_t password_length, const unsigned char *salt,
    size_t salt_length, cpkt_openssl_u64 work_factor,
    cpkt_openssl_u64 block_size, cpkt_openssl_u64 parallelization,
    cpkt_openssl_u64 maximum_memory, unsigned char *key, size_t key_length,
    OSSL_LIB_CTX *library_context, const char *property_query) {
  return EVP_PBE_scrypt_ex(password, password_length, salt, salt_length,
                           cpkt_openssl_native_u64(work_factor),
                           cpkt_openssl_native_u64(block_size),
                           cpkt_openssl_native_u64(parallelization),
                           cpkt_openssl_native_u64(maximum_memory), key,
                           key_length, library_context, property_query);
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_EVP_PKEY_CTX_ctrl_uint64. */
int cpkt_openssl_EVP_PKEY_CTX_ctrl_uint64(EVP_PKEY_CTX *context, int key_type,
                                          int operation, int command,
                                          cpkt_openssl_u64 value) {
  return EVP_PKEY_CTX_ctrl_uint64(context, key_type, operation, command,
                                  cpkt_openssl_native_u64(value));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_EVP_PKEY_CTX_set_scrypt_N. */
int cpkt_openssl_EVP_PKEY_CTX_set_scrypt_N(EVP_PKEY_CTX *context,
                                           cpkt_openssl_u64 work_factor) {
  return EVP_PKEY_CTX_set_scrypt_N(context,
                                   cpkt_openssl_native_u64(work_factor));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_EVP_PKEY_CTX_set_scrypt_maxmem_bytes. */
int cpkt_openssl_EVP_PKEY_CTX_set_scrypt_maxmem_bytes(
    EVP_PKEY_CTX *context, cpkt_openssl_u64 maximum_memory) {
  return EVP_PKEY_CTX_set_scrypt_maxmem_bytes(
      context, cpkt_openssl_native_u64(maximum_memory));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_EVP_PKEY_CTX_set_scrypt_p. */
int cpkt_openssl_EVP_PKEY_CTX_set_scrypt_p(EVP_PKEY_CTX *context,
                                           cpkt_openssl_u64 parallelization) {
  return EVP_PKEY_CTX_set_scrypt_p(context,
                                   cpkt_openssl_native_u64(parallelization));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_EVP_PKEY_CTX_set_scrypt_r. */
int cpkt_openssl_EVP_PKEY_CTX_set_scrypt_r(EVP_PKEY_CTX *context,
                                           cpkt_openssl_u64 block_size) {
  return EVP_PKEY_CTX_set_scrypt_r(context,
                                   cpkt_openssl_native_u64(block_size));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_PKCS5_pbe2_set_scrypt. */
X509_ALGOR *cpkt_openssl_PKCS5_pbe2_set_scrypt(
    const EVP_CIPHER *cipher, const unsigned char *salt, int salt_length,
    unsigned char *iv, cpkt_openssl_u64 work_factor,
    cpkt_openssl_u64 block_size, cpkt_openssl_u64 parallelization) {
  return PKCS5_pbe2_set_scrypt(cipher, salt, salt_length, iv,
                               cpkt_openssl_native_u64(work_factor),
                               cpkt_openssl_native_u64(block_size),
                               cpkt_openssl_native_u64(parallelization));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_CTX_get_domain_flags. */
int cpkt_openssl_SSL_CTX_get_domain_flags(const SSL_CTX *context,
                                          cpkt_openssl_u64 *flags_out) {
  uint64_t native_flags;
  int result;

  native_flags = 0;
  result = SSL_CTX_get_domain_flags(context,
                                    flags_out == NULL ? NULL : &native_flags);
  if (result != 0 && flags_out != NULL) {
    *flags_out = cpkt_openssl_public_u64(native_flags);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_CTX_set_domain_flags. */
int cpkt_openssl_SSL_CTX_set_domain_flags(SSL_CTX *context,
                                          cpkt_openssl_u64 flags) {
  return SSL_CTX_set_domain_flags(context, cpkt_openssl_native_u64(flags));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_get_domain_flags. */
int cpkt_openssl_SSL_get_domain_flags(const SSL *ssl,
                                      cpkt_openssl_u64 *flags_out) {
  uint64_t native_flags;
  int result;

  native_flags = 0;
  result = SSL_get_domain_flags(ssl, flags_out == NULL ? NULL : &native_flags);
  if (result != 0 && flags_out != NULL) {
    *flags_out = cpkt_openssl_public_u64(native_flags);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_accept_connection. */
SSL *cpkt_openssl_SSL_accept_connection(SSL *ssl,
                                        cpkt_openssl_u64 domain_flags) {
  return SSL_accept_connection(ssl, cpkt_openssl_native_u64(domain_flags));
}

/** Implements the documented public C89 adapter cpkt_openssl_SSL_accept_stream.
 */
SSL *cpkt_openssl_SSL_accept_stream(SSL *ssl, cpkt_openssl_u64 stream_id) {
  return SSL_accept_stream(ssl, cpkt_openssl_native_u64(stream_id));
}

/** Implements the documented public C89 adapter cpkt_openssl_SSL_get_stream_id.
 */
cpkt_openssl_u64 cpkt_openssl_SSL_get_stream_id(SSL *ssl) {
  return cpkt_openssl_public_u64(SSL_get_stream_id(ssl));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_get_stream_read_error_code. */
int cpkt_openssl_SSL_get_stream_read_error_code(
    SSL *ssl, cpkt_openssl_u64 *error_code_out) {
  uint64_t native_error_code;
  int result;

  native_error_code = 0;
  result = SSL_get_stream_read_error_code(
      ssl, error_code_out == NULL ? NULL : &native_error_code);
  if (result != 0 && error_code_out != NULL) {
    *error_code_out = cpkt_openssl_public_u64(native_error_code);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_get_stream_write_error_code. */
int cpkt_openssl_SSL_get_stream_write_error_code(
    SSL *ssl, cpkt_openssl_u64 *error_code_out) {
  uint64_t native_error_code;
  int result;

  native_error_code = 0;
  result = SSL_get_stream_write_error_code(
      ssl, error_code_out == NULL ? NULL : &native_error_code);
  if (result != 0 && error_code_out != NULL) {
    *error_code_out = cpkt_openssl_public_u64(native_error_code);
  }
  return result;
}

/** Implements the documented public C89 adapter cpkt_openssl_SSL_new_domain. */
SSL *cpkt_openssl_SSL_new_domain(SSL_CTX *context,
                                 cpkt_openssl_u64 domain_flags) {
  return SSL_new_domain(context, cpkt_openssl_native_u64(domain_flags));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_new_from_listener. */
SSL *cpkt_openssl_SSL_new_from_listener(SSL *listener,
                                        cpkt_openssl_u64 stream_id) {
  return SSL_new_from_listener(listener, cpkt_openssl_native_u64(stream_id));
}

/** Implements the documented public C89 adapter cpkt_openssl_SSL_new_listener.
 */
SSL *cpkt_openssl_SSL_new_listener(SSL_CTX *context,
                                   cpkt_openssl_u64 domain_flags) {
  return SSL_new_listener(context, cpkt_openssl_native_u64(domain_flags));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_new_listener_from. */
SSL *cpkt_openssl_SSL_new_listener_from(SSL *ssl,
                                        cpkt_openssl_u64 domain_flags) {
  return SSL_new_listener_from(ssl, cpkt_openssl_native_u64(domain_flags));
}

/** Implements the documented public C89 adapter cpkt_openssl_SSL_new_stream. */
SSL *cpkt_openssl_SSL_new_stream(SSL *ssl, cpkt_openssl_u64 stream_id) {
  return SSL_new_stream(ssl, cpkt_openssl_native_u64(stream_id));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_set_incoming_stream_policy. */
int cpkt_openssl_SSL_set_incoming_stream_policy(
    SSL *ssl, int policy, cpkt_openssl_u64 application_error_code) {
  return SSL_set_incoming_stream_policy(
      ssl, policy, cpkt_openssl_native_u64(application_error_code));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_stream_conclude. */
int cpkt_openssl_SSL_stream_conclude(SSL *ssl,
                                     cpkt_openssl_u64 application_error_code) {
  return SSL_stream_conclude(ssl,
                             cpkt_openssl_native_u64(application_error_code));
}

/** Implements the documented public C89 adapter cpkt_openssl_SSL_write_ex2. */
int cpkt_openssl_SSL_write_ex2(SSL *ssl, const void *buffer,
                               size_t buffer_length, cpkt_openssl_u64 flags,
                               size_t *written_out) {
  return SSL_write_ex2(ssl, buffer, buffer_length,
                       cpkt_openssl_native_u64(flags), written_out);
}

/** Implements the documented public C89 adapter cpkt_openssl_BIO_number_read.
 */
cpkt_openssl_u64 cpkt_openssl_BIO_number_read(BIO *bio) {
  return cpkt_openssl_public_u64(BIO_number_read(bio));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_BIO_number_written. */
cpkt_openssl_u64 cpkt_openssl_BIO_number_written(BIO *bio) {
  return cpkt_openssl_public_u64(BIO_number_written(bio));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OSSL_HPKE_CTX_get_seq. */
int cpkt_openssl_OSSL_HPKE_CTX_get_seq(OSSL_HPKE_CTX *context,
                                       cpkt_openssl_u64 *sequence_out) {
  uint64_t native_sequence;
  int result;

  native_sequence = 0;
  result = OSSL_HPKE_CTX_get_seq(
      context, sequence_out == NULL ? NULL : &native_sequence);
  if (result != 0 && sequence_out != NULL) {
    *sequence_out = cpkt_openssl_public_u64(native_sequence);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OSSL_HPKE_CTX_set_seq. */
int cpkt_openssl_OSSL_HPKE_CTX_set_seq(OSSL_HPKE_CTX *context,
                                       cpkt_openssl_u64 sequence) {
  return OSSL_HPKE_CTX_set_seq(context, cpkt_openssl_native_u64(sequence));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OSSL_PARAM_BLD_push_int64. */
int cpkt_openssl_OSSL_PARAM_BLD_push_int64(OSSL_PARAM_BLD *builder,
                                           const char *key,
                                           cpkt_openssl_i64 value) {
  return OSSL_PARAM_BLD_push_int64(builder, key,
                                   cpkt_openssl_native_i64(value));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OSSL_PARAM_BLD_push_uint64. */
int cpkt_openssl_OSSL_PARAM_BLD_push_uint64(OSSL_PARAM_BLD *builder,
                                            const char *key,
                                            cpkt_openssl_u64 value) {
  return OSSL_PARAM_BLD_push_uint64(builder, key,
                                    cpkt_openssl_native_u64(value));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OSSL_PARAM_construct_int64. */
int cpkt_openssl_OSSL_PARAM_construct_int64(
    cpkt_openssl_param_i64 **parameter_out, const char *key,
    cpkt_openssl_i64 value) {
  cpkt_openssl_param_i64 *parameter;

  if (parameter_out == NULL) {
    return 0;
  }
  *parameter_out = NULL;
  parameter = (cpkt_openssl_param_i64 *)malloc(sizeof(*parameter));
  if (parameter == NULL) {
    return 0;
  }
  parameter->value = cpkt_openssl_native_i64(value);
  parameter->native = OSSL_PARAM_construct_int64(key, &parameter->value);
  *parameter_out = parameter;
  return 1;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OSSL_PARAM_construct_uint64. */
int cpkt_openssl_OSSL_PARAM_construct_uint64(
    cpkt_openssl_param_u64 **parameter_out, const char *key,
    cpkt_openssl_u64 value) {
  cpkt_openssl_param_u64 *parameter;

  if (parameter_out == NULL) {
    return 0;
  }
  *parameter_out = NULL;
  parameter = (cpkt_openssl_param_u64 *)malloc(sizeof(*parameter));
  if (parameter == NULL) {
    return 0;
  }
  parameter->value = cpkt_openssl_native_u64(value);
  parameter->native = OSSL_PARAM_construct_uint64(key, &parameter->value);
  *parameter_out = parameter;
  return 1;
}

/** Implements the documented public C89 facade operation
 * cpkt_openssl_param_i64_native. */
OSSL_PARAM *cpkt_openssl_param_i64_native(cpkt_openssl_param_i64 *parameter) {
  return parameter == NULL ? NULL : &parameter->native;
}

/** Implements the documented public C89 facade operation
 * cpkt_openssl_param_i64_value. */
cpkt_openssl_i64
cpkt_openssl_param_i64_value(const cpkt_openssl_param_i64 *parameter) {
  return parameter == NULL ? cpkt_openssl_i64_make(0UL, 0UL)
                           : cpkt_openssl_public_i64(parameter->value);
}

/** Implements the documented public C89 facade operation
 * cpkt_openssl_param_i64_set_value. */
void cpkt_openssl_param_i64_set_value(cpkt_openssl_param_i64 *parameter,
                                      cpkt_openssl_i64 value) {
  if (parameter != NULL) {
    parameter->value = cpkt_openssl_native_i64(value);
  }
}

/** Implements the documented public C89 facade operation
 * cpkt_openssl_param_i64_free. */
void cpkt_openssl_param_i64_free(cpkt_openssl_param_i64 *parameter) {
  free(parameter);
}

/** Implements the documented public C89 facade operation
 * cpkt_openssl_param_u64_native. */
OSSL_PARAM *cpkt_openssl_param_u64_native(cpkt_openssl_param_u64 *parameter) {
  return parameter == NULL ? NULL : &parameter->native;
}

/** Implements the documented public C89 facade operation
 * cpkt_openssl_param_u64_value. */
cpkt_openssl_u64
cpkt_openssl_param_u64_value(const cpkt_openssl_param_u64 *parameter) {
  return parameter == NULL ? cpkt_openssl_u64_make(0UL, 0UL)
                           : cpkt_openssl_public_u64(parameter->value);
}

/** Implements the documented public C89 facade operation
 * cpkt_openssl_param_u64_set_value. */
void cpkt_openssl_param_u64_set_value(cpkt_openssl_param_u64 *parameter,
                                      cpkt_openssl_u64 value) {
  if (parameter != NULL) {
    parameter->value = cpkt_openssl_native_u64(value);
  }
}

/** Implements the documented public C89 facade operation
 * cpkt_openssl_param_u64_free. */
void cpkt_openssl_param_u64_free(cpkt_openssl_param_u64 *parameter) {
  free(parameter);
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OSSL_PARAM_get_int64. */
int cpkt_openssl_OSSL_PARAM_get_int64(const OSSL_PARAM *parameter,
                                      cpkt_openssl_i64 *value_out) {
  int64_t native_value;
  int result;

  native_value = 0;
  result =
      OSSL_PARAM_get_int64(parameter, value_out == NULL ? NULL : &native_value);
  if (result != 0 && value_out != NULL) {
    *value_out = cpkt_openssl_public_i64(native_value);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OSSL_PARAM_get_uint64. */
int cpkt_openssl_OSSL_PARAM_get_uint64(const OSSL_PARAM *parameter,
                                       cpkt_openssl_u64 *value_out) {
  uint64_t native_value;
  int result;

  native_value = 0;
  result = OSSL_PARAM_get_uint64(parameter,
                                 value_out == NULL ? NULL : &native_value);
  if (result != 0 && value_out != NULL) {
    *value_out = cpkt_openssl_public_u64(native_value);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OSSL_PARAM_set_int64. */
int cpkt_openssl_OSSL_PARAM_set_int64(OSSL_PARAM *parameter,
                                      cpkt_openssl_i64 value) {
  return OSSL_PARAM_set_int64(parameter, cpkt_openssl_native_i64(value));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_OSSL_PARAM_set_uint64. */
int cpkt_openssl_OSSL_PARAM_set_uint64(OSSL_PARAM *parameter,
                                       cpkt_openssl_u64 value) {
  return OSSL_PARAM_set_uint64(parameter, cpkt_openssl_native_u64(value));
}

/** Implements the documented public C89 facade operation
 * cpkt_openssl_SHA512_CTX_new. */
cpkt_openssl_sha512_context *cpkt_openssl_SHA512_CTX_new(void) {
  return (cpkt_openssl_sha512_context *)calloc(
      1, sizeof(cpkt_openssl_sha512_context));
}

/** Implements the documented public C89 facade operation
 * cpkt_openssl_SHA512_CTX_free. */
void cpkt_openssl_SHA512_CTX_free(cpkt_openssl_sha512_context *context) {
  free(context);
}

/** Implements the documented public C89 adapter cpkt_openssl_SHA384_Init. */
int cpkt_openssl_SHA384_Init(cpkt_openssl_sha512_context *context) {
  return context == NULL ? 0 : SHA384_Init(&context->native);
}

/** Implements the documented public C89 adapter cpkt_openssl_SHA384_Update. */
int cpkt_openssl_SHA384_Update(cpkt_openssl_sha512_context *context,
                               const void *data, size_t length) {
  return context == NULL ? 0 : SHA384_Update(&context->native, data, length);
}

/** Implements the documented public C89 adapter cpkt_openssl_SHA384_Final. */
int cpkt_openssl_SHA384_Final(unsigned char *digest,
                              cpkt_openssl_sha512_context *context) {
  return context == NULL ? 0 : SHA384_Final(digest, &context->native);
}

/** Implements the documented public C89 adapter cpkt_openssl_SHA512_Init. */
int cpkt_openssl_SHA512_Init(cpkt_openssl_sha512_context *context) {
  return context == NULL ? 0 : SHA512_Init(&context->native);
}

/** Implements the documented public C89 adapter cpkt_openssl_SHA512_Update. */
int cpkt_openssl_SHA512_Update(cpkt_openssl_sha512_context *context,
                               const void *data, size_t length) {
  return context == NULL ? 0 : SHA512_Update(&context->native, data, length);
}

/** Implements the documented public C89 adapter cpkt_openssl_SHA512_Final. */
int cpkt_openssl_SHA512_Final(unsigned char *digest,
                              cpkt_openssl_sha512_context *context) {
  return context == NULL ? 0 : SHA512_Final(digest, &context->native);
}

/** Implements the documented public C89 adapter cpkt_openssl_SHA512_Transform.
 */
void cpkt_openssl_SHA512_Transform(cpkt_openssl_sha512_context *context,
                                   const unsigned char *block) {
  if (context != NULL) {
    SHA512_Transform(&context->native, block);
  }
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_get_value_uint. */
int cpkt_openssl_SSL_get_value_uint(SSL *ssl, unsigned long value_class,
                                    unsigned long value_id,
                                    cpkt_openssl_u64 *value_out) {
  uint64_t native_value;
  int result;

  if (value_out != NULL) {
    *value_out = cpkt_openssl_u64_make(0UL, 0UL);
  }
  if (value_class > UINT32_MAX || value_id > UINT32_MAX) {
    return 0;
  }
  native_value = 0;
  result = SSL_get_value_uint(ssl, (uint32_t)value_class, (uint32_t)value_id,
                              value_out == NULL ? NULL : &native_value);
  if (result != 0 && value_out != NULL) {
    *value_out = cpkt_openssl_public_u64(native_value);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_set_value_uint. */
int cpkt_openssl_SSL_set_value_uint(SSL *ssl, unsigned long value_class,
                                    unsigned long value_id,
                                    cpkt_openssl_u64 value) {
  if (value_class > UINT32_MAX || value_id > UINT32_MAX) {
    return 0;
  }
  return SSL_set_value_uint(ssl, (uint32_t)value_class, (uint32_t)value_id,
                            cpkt_openssl_native_u64(value));
}

/** Implements the documented public C89 facade operation
 * cpkt_openssl_atomic_u64_new. */
cpkt_openssl_atomic_u64 *
cpkt_openssl_atomic_u64_new(cpkt_openssl_u64 initial_value) {
  cpkt_openssl_atomic_u64 *value;

  value = (cpkt_openssl_atomic_u64 *)malloc(sizeof(*value));
  if (value != NULL) {
    value->native = cpkt_openssl_native_u64(initial_value);
  }
  return value;
}

/** Implements the documented public C89 facade operation
 * cpkt_openssl_atomic_u64_free. */
void cpkt_openssl_atomic_u64_free(cpkt_openssl_atomic_u64 *value) {
  free(value);
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_CRYPTO_atomic_add64. */
int cpkt_openssl_CRYPTO_atomic_add64(cpkt_openssl_atomic_u64 *value,
                                     cpkt_openssl_u64 amount,
                                     cpkt_openssl_u64 *result_out,
                                     CRYPTO_RWLOCK *lock) {
  uint64_t result;
  int status;

  if (value == NULL) {
    return 0;
  }
  result = 0;
  status = CRYPTO_atomic_add64(&value->native, cpkt_openssl_native_u64(amount),
                               result_out == NULL ? NULL : &result, lock);
  if (status != 0 && result_out != NULL) {
    *result_out = cpkt_openssl_public_u64(result);
  }
  return status;
}

/** Implements the documented public C89 adapter cpkt_openssl_CRYPTO_atomic_and.
 */
int cpkt_openssl_CRYPTO_atomic_and(cpkt_openssl_atomic_u64 *value,
                                   cpkt_openssl_u64 mask,
                                   cpkt_openssl_u64 *result_out,
                                   CRYPTO_RWLOCK *lock) {
  uint64_t result;
  int status;

  if (value == NULL) {
    return 0;
  }
  result = 0;
  status = CRYPTO_atomic_and(&value->native, cpkt_openssl_native_u64(mask),
                             result_out == NULL ? NULL : &result, lock);
  if (status != 0 && result_out != NULL) {
    *result_out = cpkt_openssl_public_u64(result);
  }
  return status;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_CRYPTO_atomic_load. */
int cpkt_openssl_CRYPTO_atomic_load(cpkt_openssl_atomic_u64 *value,
                                    cpkt_openssl_u64 *result_out,
                                    CRYPTO_RWLOCK *lock) {
  uint64_t result;
  int status;

  if (value == NULL) {
    return 0;
  }
  result = 0;
  status = CRYPTO_atomic_load(&value->native,
                              result_out == NULL ? NULL : &result, lock);
  if (status != 0 && result_out != NULL) {
    *result_out = cpkt_openssl_public_u64(result);
  }
  return status;
}

/** Implements the documented public C89 adapter cpkt_openssl_CRYPTO_atomic_or.
 */
int cpkt_openssl_CRYPTO_atomic_or(cpkt_openssl_atomic_u64 *value,
                                  cpkt_openssl_u64 mask,
                                  cpkt_openssl_u64 *result_out,
                                  CRYPTO_RWLOCK *lock) {
  uint64_t result;
  int status;

  if (value == NULL) {
    return 0;
  }
  result = 0;
  status = CRYPTO_atomic_or(&value->native, cpkt_openssl_native_u64(mask),
                            result_out == NULL ? NULL : &result, lock);
  if (status != 0 && result_out != NULL) {
    *result_out = cpkt_openssl_public_u64(result);
  }
  return status;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_CRYPTO_atomic_store. */
int cpkt_openssl_CRYPTO_atomic_store(cpkt_openssl_atomic_u64 *value,
                                     cpkt_openssl_u64 replacement,
                                     CRYPTO_RWLOCK *lock) {
  if (value == NULL) {
    return 0;
  }
  return CRYPTO_atomic_store(&value->native,
                             cpkt_openssl_native_u64(replacement), lock);
}

/** Implements the documented public C89 adapter cpkt_openssl_SSL_shutdown_ex.
 */
int cpkt_openssl_SSL_shutdown_ex(
    SSL *ssl, cpkt_openssl_u64 flags,
    const cpkt_openssl_ssl_shutdown_args *arguments, size_t arguments_length) {
  SSL_SHUTDOWN_EX_ARGS native_arguments;

  if (arguments == NULL) {
    return SSL_shutdown_ex(ssl, cpkt_openssl_native_u64(flags), NULL,
                           arguments_length);
  }
  native_arguments.quic_error_code =
      cpkt_openssl_native_u64(arguments->quic_error_code);
  native_arguments.quic_reason = arguments->quic_reason;
  return SSL_shutdown_ex(ssl, cpkt_openssl_native_u64(flags), &native_arguments,
                         cpkt_openssl_native_length(arguments_length,
                                                    sizeof(*arguments),
                                                    sizeof(native_arguments)));
}

/** Implements the documented public C89 adapter cpkt_openssl_SSL_stream_reset.
 */
int cpkt_openssl_SSL_stream_reset(
    SSL *ssl, const cpkt_openssl_ssl_stream_reset_args *arguments,
    size_t arguments_length) {
  SSL_STREAM_RESET_ARGS native_arguments;

  if (arguments == NULL) {
    return SSL_stream_reset(ssl, NULL, arguments_length);
  }
  native_arguments.quic_error_code =
      cpkt_openssl_native_u64(arguments->quic_error_code);
  return SSL_stream_reset(ssl, &native_arguments,
                          cpkt_openssl_native_length(arguments_length,
                                                     sizeof(*arguments),
                                                     sizeof(native_arguments)));
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_get_conn_close_info. */
int cpkt_openssl_SSL_get_conn_close_info(
    SSL *ssl, cpkt_openssl_ssl_conn_close_info *information_out,
    size_t information_length) {
  SSL_CONN_CLOSE_INFO native_information;
  int result;

  if (information_out == NULL) {
    return SSL_get_conn_close_info(ssl, NULL, information_length);
  }
  memset(&native_information, 0, sizeof(native_information));
  result = SSL_get_conn_close_info(
      ssl, &native_information,
      cpkt_openssl_native_length(information_length, sizeof(*information_out),
                                 sizeof(native_information)));
  if (result != 0) {
    information_out->error_code =
        cpkt_openssl_public_u64(native_information.error_code);
    information_out->frame_type =
        cpkt_openssl_public_u64(native_information.frame_type);
    information_out->reason = native_information.reason;
    information_out->reason_length = native_information.reason_len;
    information_out->flags = (unsigned long)native_information.flags;
  }
  return result;
}

/** Implements the documented public C89 adapter cpkt_openssl_BIO_recvmmsg. */
int cpkt_openssl_BIO_recvmmsg(BIO *bio, cpkt_openssl_bio_message *messages,
                              size_t message_stride, size_t message_count,
                              cpkt_openssl_u64 flags, size_t *processed_out) {
  BIO_MSG *native_messages;
  int result;

  if (message_count == 0)
    return BIO_recvmmsg(bio, NULL, sizeof(*native_messages), 0,
                        cpkt_openssl_native_u64(flags), processed_out);
  if (messages == NULL ||
      !cpkt_openssl_record_span_valid(message_count, message_stride,
                                      sizeof(*messages)) ||
      message_count > SIZE_MAX / sizeof(*native_messages)) {
    return 0;
  }
  native_messages = (BIO_MSG *)calloc(message_count, sizeof(*native_messages));
  if (native_messages == NULL && message_count != 0) {
    return 0;
  }
  cpkt_openssl_copy_bio_messages_to_native(native_messages,
                                           sizeof(*native_messages), messages,
                                           message_stride, message_count);
  result = BIO_recvmmsg(bio, native_messages, sizeof(*native_messages),
                        message_count, cpkt_openssl_native_u64(flags),
                        processed_out);
  cpkt_openssl_copy_bio_messages_from_native(
      messages, message_stride, native_messages, sizeof(*native_messages),
      message_count);
  free(native_messages);
  return result;
}

/** Implements the documented public C89 adapter cpkt_openssl_BIO_sendmmsg. */
int cpkt_openssl_BIO_sendmmsg(BIO *bio, cpkt_openssl_bio_message *messages,
                              size_t message_stride, size_t message_count,
                              cpkt_openssl_u64 flags, size_t *processed_out) {
  BIO_MSG *native_messages;
  int result;

  if (message_count == 0)
    return BIO_sendmmsg(bio, NULL, sizeof(*native_messages), 0,
                        cpkt_openssl_native_u64(flags), processed_out);
  if (messages == NULL ||
      !cpkt_openssl_record_span_valid(message_count, message_stride,
                                      sizeof(*messages)) ||
      message_count > SIZE_MAX / sizeof(*native_messages)) {
    return 0;
  }
  native_messages = (BIO_MSG *)calloc(message_count, sizeof(*native_messages));
  if (native_messages == NULL && message_count != 0) {
    return 0;
  }
  cpkt_openssl_copy_bio_messages_to_native(native_messages,
                                           sizeof(*native_messages), messages,
                                           message_stride, message_count);
  result = BIO_sendmmsg(bio, native_messages, sizeof(*native_messages),
                        message_count, cpkt_openssl_native_u64(flags),
                        processed_out);
  cpkt_openssl_copy_bio_messages_from_native(
      messages, message_stride, native_messages, sizeof(*native_messages),
      message_count);
  free(native_messages);
  return result;
}

/** Implements the documented public C89 adapter cpkt_openssl_BIO_meth_new. */
cpkt_openssl_bio_method *cpkt_openssl_BIO_meth_new(int type, const char *name) {
  cpkt_openssl_bio_method *method;

  method = (cpkt_openssl_bio_method *)calloc(1U, sizeof(*method));
  if (method == NULL) {
    return NULL;
  }
  method->native = BIO_meth_new(type, name);
  method->lock = CRYPTO_THREAD_lock_new();
  if (method->native == NULL || method->lock == NULL) {
    BIO_meth_free(method->native);
    CRYPTO_THREAD_lock_free(method->lock);
    free(method);
    return NULL;
  }
  return method;
}

/** Implements the documented public C89 adapter cpkt_openssl_BIO_meth_close. */
int cpkt_openssl_BIO_meth_close(cpkt_openssl_bio_method *method) {
  BIO_METHOD *native_method;
  CRYPTO_RWLOCK *lock;

  if (method == NULL || method->lock == NULL ||
      !CRYPTO_THREAD_write_lock(method->lock)) {
    return 0;
  }
  if (method->closing || method->active_bios != 0U ||
      method->active_callbacks != 0U) {
    CRYPTO_THREAD_unlock(method->lock);
    return 0;
  }
  method->closing = 1;
  native_method = method->native;
  method->native = NULL;
  lock = method->lock;
  CRYPTO_THREAD_unlock(lock);
  BIO_meth_free(native_method);
  CRYPTO_THREAD_lock_free(lock);
  free(method);
  return 1;
}

/** Implements the documented public C89 adapter cpkt_openssl_BIO_meth_native.
 */
const BIO_METHOD *
cpkt_openssl_BIO_meth_native(const cpkt_openssl_bio_method *method) {
  if (method == NULL || method->closing) {
    return NULL;
  }
  return method->native;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_BIO_meth_set_sendmmsg. */
int cpkt_openssl_BIO_meth_set_sendmmsg(
    cpkt_openssl_bio_method *method, cpkt_openssl_bio_mmsg_callback callback) {
  int result;

  if (method == NULL || method->lock == NULL ||
      !CRYPTO_THREAD_write_lock(method->lock) || method->closing) {
    return 0;
  }
  result = BIO_meth_set_sendmmsg(
      method->native,
      callback == NULL ? NULL : cpkt_openssl_bio_sendmmsg_callback);
  if (result != 0) {
    method->send_callback = callback;
  }
  CRYPTO_THREAD_unlock(method->lock);
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_BIO_meth_get_sendmmsg. */
cpkt_openssl_bio_mmsg_callback
cpkt_openssl_BIO_meth_get_sendmmsg(const cpkt_openssl_bio_method *method) {
  cpkt_openssl_bio_mmsg_callback callback;
  cpkt_openssl_bio_method *mutable_method;

  if (method == NULL) {
    return NULL;
  }
  mutable_method = (cpkt_openssl_bio_method *)method;
  if (!CRYPTO_THREAD_read_lock(mutable_method->lock) ||
      mutable_method->closing) {
    return NULL;
  }
  callback = mutable_method->send_callback;
  CRYPTO_THREAD_unlock(mutable_method->lock);
  return callback;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_BIO_meth_set_recvmmsg. */
int cpkt_openssl_BIO_meth_set_recvmmsg(
    cpkt_openssl_bio_method *method, cpkt_openssl_bio_mmsg_callback callback) {
  int result;

  if (method == NULL || method->lock == NULL ||
      !CRYPTO_THREAD_write_lock(method->lock) || method->closing) {
    return 0;
  }
  result = BIO_meth_set_recvmmsg(
      method->native,
      callback == NULL ? NULL : cpkt_openssl_bio_recvmmsg_callback);
  if (result != 0) {
    method->receive_callback = callback;
  }
  CRYPTO_THREAD_unlock(method->lock);
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_BIO_meth_get_recvmmsg. */
cpkt_openssl_bio_mmsg_callback
cpkt_openssl_BIO_meth_get_recvmmsg(const cpkt_openssl_bio_method *method) {
  cpkt_openssl_bio_mmsg_callback callback;
  cpkt_openssl_bio_method *mutable_method;

  if (method == NULL) {
    return NULL;
  }
  mutable_method = (cpkt_openssl_bio_method *)method;
  if (!CRYPTO_THREAD_read_lock(mutable_method->lock) ||
      mutable_method->closing) {
    return NULL;
  }
  callback = mutable_method->receive_callback;
  CRYPTO_THREAD_unlock(mutable_method->lock);
  return callback;
}

/** Implements the documented public C89 adapter cpkt_openssl_BIO_new. */
cpkt_openssl_bio *cpkt_openssl_BIO_new(cpkt_openssl_bio_method *method,
                                       void *callback_context) {
  return cpkt_openssl_bio_new_internal(NULL, method, callback_context);
}

/** Implements the documented public C89 adapter cpkt_openssl_BIO_new_ex. */
cpkt_openssl_bio *cpkt_openssl_BIO_new_ex(OSSL_LIB_CTX *library_context,
                                          cpkt_openssl_bio_method *method,
                                          void *callback_context) {
  return cpkt_openssl_bio_new_internal(library_context, method,
                                       callback_context);
}

/** Implements the documented public C89 adapter cpkt_openssl_BIO_close. */
int cpkt_openssl_BIO_close(cpkt_openssl_bio *bio) {
  cpkt_openssl_bio_method *method;
  BIO *native_bio;

  if (bio == NULL) {
    return 1;
  }
  method = bio->method;
  if (method == NULL || method->lock == NULL ||
      !CRYPTO_THREAD_write_lock(method->lock)) {
    return 0;
  }
  if (method->closing || method->active_callbacks != 0U) {
    CRYPTO_THREAD_unlock(method->lock);
    return 0;
  }
  native_bio = bio->native;
  bio->native = NULL;
  bio->callback = NULL;
  bio->callback_ex = NULL;
  /* BIO_free emits BIO_CB_FREE through a configured generic callback. Detach
   * generic callbacks before release. Keep the callback argument for batch
   * methods until the last native BIO reference frees its ex-data shell. */
  BIO_set_callback(native_bio, NULL);
  BIO_set_callback_ex(native_bio, NULL);
  CRYPTO_THREAD_unlock(method->lock);
  return BIO_free(native_bio);
}

/** Implements the documented public C89 adapter cpkt_openssl_BIO_native. */
BIO *cpkt_openssl_BIO_native(cpkt_openssl_bio *bio) {
  return bio == NULL ? NULL : bio->native;
}

/** Implements the documented public C89 adapter cpkt_openssl_BIO_set_callback.
 */
void cpkt_openssl_BIO_set_callback(cpkt_openssl_bio *bio,
                                   cpkt_openssl_bio_callback callback) {
  cpkt_openssl_bio_method *method;

  if (bio == NULL || bio->method == NULL) {
    return;
  }
  method = bio->method;
  if (!CRYPTO_THREAD_write_lock(method->lock) || method->closing) {
    return;
  }
  bio->callback = callback;
  BIO_set_callback(bio->native, callback == NULL
                                    ? NULL
                                    : cpkt_openssl_bio_callback_trampoline);
  CRYPTO_THREAD_unlock(method->lock);
}

/** Implements the documented public C89 adapter cpkt_openssl_BIO_get_callback.
 */
cpkt_openssl_bio_callback
cpkt_openssl_BIO_get_callback(const cpkt_openssl_bio *bio) {
  cpkt_openssl_bio *mutable_bio;
  cpkt_openssl_bio_method *method;
  cpkt_openssl_bio_callback callback;

  if (bio == NULL || bio->method == NULL) {
    return NULL;
  }
  mutable_bio = (cpkt_openssl_bio *)bio;
  method = mutable_bio->method;
  if (!CRYPTO_THREAD_read_lock(method->lock) || method->closing) {
    return NULL;
  }
  callback = mutable_bio->callback;
  CRYPTO_THREAD_unlock(method->lock);
  return callback;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_BIO_set_callback_ex. */
void cpkt_openssl_BIO_set_callback_ex(cpkt_openssl_bio *bio,
                                      cpkt_openssl_bio_callback_ex callback) {
  cpkt_openssl_bio_method *method;

  if (bio == NULL || bio->method == NULL) {
    return;
  }
  method = bio->method;
  if (!CRYPTO_THREAD_write_lock(method->lock) || method->closing) {
    return;
  }
  bio->callback_ex = callback;
  BIO_set_callback_ex(
      bio->native,
      callback == NULL ? NULL : cpkt_openssl_bio_callback_ex_trampoline);
  CRYPTO_THREAD_unlock(method->lock);
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_BIO_get_callback_ex. */
cpkt_openssl_bio_callback_ex
cpkt_openssl_BIO_get_callback_ex(const cpkt_openssl_bio *bio) {
  cpkt_openssl_bio *mutable_bio;
  cpkt_openssl_bio_method *method;
  cpkt_openssl_bio_callback_ex callback;

  if (bio == NULL || bio->method == NULL) {
    return NULL;
  }
  mutable_bio = (cpkt_openssl_bio *)bio;
  method = mutable_bio->method;
  if (!CRYPTO_THREAD_read_lock(method->lock) || method->closing) {
    return NULL;
  }
  callback = mutable_bio->callback_ex;
  CRYPTO_THREAD_unlock(method->lock);
  return callback;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_BIO_get_rpoll_descriptor. */
int cpkt_openssl_BIO_get_rpoll_descriptor(
    BIO *bio, cpkt_openssl_poll_descriptor *descriptor_out) {
  BIO_POLL_DESCRIPTOR native_descriptor;
  int result;

  if (descriptor_out == NULL) {
    return 0;
  }
  result = BIO_get_rpoll_descriptor(bio, &native_descriptor);
  if (result != 0) {
    cpkt_openssl_copy_poll_descriptor_from_native(descriptor_out,
                                                  &native_descriptor);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_BIO_get_wpoll_descriptor. */
int cpkt_openssl_BIO_get_wpoll_descriptor(
    BIO *bio, cpkt_openssl_poll_descriptor *descriptor_out) {
  BIO_POLL_DESCRIPTOR native_descriptor;
  int result;

  if (descriptor_out == NULL) {
    return 0;
  }
  result = BIO_get_wpoll_descriptor(bio, &native_descriptor);
  if (result != 0) {
    cpkt_openssl_copy_poll_descriptor_from_native(descriptor_out,
                                                  &native_descriptor);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_get_rpoll_descriptor. */
int cpkt_openssl_SSL_get_rpoll_descriptor(
    SSL *ssl, cpkt_openssl_poll_descriptor *descriptor_out) {
  BIO_POLL_DESCRIPTOR native_descriptor;
  int result;

  if (descriptor_out == NULL) {
    return 0;
  }
  result = SSL_get_rpoll_descriptor(ssl, &native_descriptor);
  if (result != 0) {
    cpkt_openssl_copy_poll_descriptor_from_native(descriptor_out,
                                                  &native_descriptor);
  }
  return result;
}

/** Implements the documented public C89 adapter
 * cpkt_openssl_SSL_get_wpoll_descriptor. */
int cpkt_openssl_SSL_get_wpoll_descriptor(
    SSL *ssl, cpkt_openssl_poll_descriptor *descriptor_out) {
  BIO_POLL_DESCRIPTOR native_descriptor;
  int result;

  if (descriptor_out == NULL) {
    return 0;
  }
  result = SSL_get_wpoll_descriptor(ssl, &native_descriptor);
  if (result != 0) {
    cpkt_openssl_copy_poll_descriptor_from_native(descriptor_out,
                                                  &native_descriptor);
  }
  return result;
}

/** Implements the documented public C89 helper
 * cpkt_openssl_SSL_as_poll_descriptor. */
void cpkt_openssl_SSL_as_poll_descriptor(
    SSL *ssl, cpkt_openssl_poll_descriptor *descriptor_out) {
  if (descriptor_out == NULL) {
    return;
  }
  memset(descriptor_out, 0, sizeof(*descriptor_out));
  descriptor_out->type = BIO_POLL_DESCRIPTOR_TYPE_SSL;
  descriptor_out->value_kind = CPKT_OPENSSL_POLL_VALUE_SSL;
  descriptor_out->ssl = ssl;
}

/** Implements the documented public C89 adapter cpkt_openssl_SSL_poll. */
int cpkt_openssl_SSL_poll(cpkt_openssl_ssl_poll_item *items, size_t item_count,
                          size_t item_stride, const struct timeval *timeout,
                          cpkt_openssl_u64 flags, size_t *result_count_out) {
  SSL_POLL_ITEM *native_items;
  int result;

  if (item_count == 0)
    return SSL_poll(NULL, 0, 0, timeout, cpkt_openssl_native_u64(flags),
                    result_count_out);
  if (items == NULL ||
      !cpkt_openssl_record_span_valid(item_count, item_stride,
                                      sizeof(*items)) ||
      item_count > SIZE_MAX / sizeof(*native_items)) {
    return 0;
  }
  native_items = (SSL_POLL_ITEM *)calloc(item_count, sizeof(*native_items));
  if (native_items == NULL && item_count != 0) {
    return 0;
  }
  if (!cpkt_openssl_copy_poll_items_to_native(native_items, items, item_stride,
                                              item_count)) {
    free(native_items);
    return 0;
  }
  result = SSL_poll(native_items, item_count, sizeof(*native_items), timeout,
                    cpkt_openssl_native_u64(flags), result_count_out);
  cpkt_openssl_copy_poll_items_from_native(items, item_stride, native_items,
                                           item_count);
  free(native_items);
  return result;
}
