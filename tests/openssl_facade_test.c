#include <cpkt/openssl.h>

#include <limits.h>
#include <string.h>

typedef struct openssl_mmsg_test_context {
  cpkt_openssl_bio *bio;
  int calls;
  int close_result;
  int legacy_callback_calls;
  int extended_callback_calls;
  int regular_callback_calls;
  int regular_ex_callback_calls;
} openssl_mmsg_test_context;

typedef struct openssl_padded_native_message {
  BIO_MSG message;
  unsigned char padding[7];
} openssl_padded_native_message;

static int openssl_mmsg_test_messages(const cpkt_openssl_bio_message *messages,
                                      size_t message_count) {
  if (message_count == 1U) {
    return 1;
  }
  if (message_count != 2U || messages[0].data == 0 || messages[1].data == 0 ||
      messages[0].data_length != 1U || messages[1].data_length != 2U ||
      memcmp(messages[0].data, "a", 1U) != 0 ||
      memcmp(messages[1].data, "bc", 2U) != 0) {
    return 0;
  }
  return 1;
}

static int openssl_mmsg_test_create(BIO *bio) {
  BIO_set_init(bio, 1);
  return 1;
}

static int openssl_mmsg_test_write(BIO *bio, const char *data, int size) {
  (void)bio;
  (void)data;
  return size;
}

static int openssl_mmsg_destroy_calls;
static BIO *openssl_duplicate_source;

static long openssl_mmsg_test_ctrl(BIO *bio, int command, long number,
                                   void *argument) {
  (void)bio;
  (void)number;
  (void)argument;
  return command == BIO_CTRL_DUP ? 1L : 0L;
}

static long openssl_duplicate_callback(void *context, BIO *bio, int operation,
                                       const void *argument,
                                       int argument_integer, long argument_long,
                                       long result) {
  (void)context;
  (void)operation;
  (void)argument;
  (void)argument_integer;
  (void)argument_long;
  return bio == openssl_duplicate_source ? result : 0L;
}

static int openssl_mmsg_test_destroy(BIO *bio) {
  (void)bio;
  ++openssl_mmsg_destroy_calls;
  return 1;
}

static int openssl_retained_mmsg_callback(void *context, BIO *bio,
                                          cpkt_openssl_bio_message *messages,
                                          size_t message_stride,
                                          size_t message_count,
                                          cpkt_openssl_u64 flags,
                                          size_t *processed_out) {
  int *calls;
  calls = (int *)context;
  if (calls == 0 || bio == 0 || messages == 0 ||
      message_stride != sizeof(*messages) || message_count != 1U ||
      !cpkt_openssl_u64_is_zero(flags) || processed_out == 0)
    return 0;
  ++*calls;
  *processed_out = message_count;
  return 1;
}

static long
openssl_mmsg_test_regular_callback(void *context, BIO *bio, int operation,
                                   const void *argument, int argument_integer,
                                   long argument_long, long result) {
  openssl_mmsg_test_context *test_context;
  (void)argument_long;
  test_context = (openssl_mmsg_test_context *)context;
  if (test_context == 0 || bio != cpkt_openssl_BIO_native(test_context->bio) ||
      (operation & ~BIO_CB_RETURN) != BIO_CB_WRITE || argument == 0 ||
      argument_integer != 3 || memcmp(argument, "abc", 3U) != 0)
    return 0L;
  ++test_context->regular_callback_calls;
  return result;
}

static long openssl_mmsg_test_regular_ex_callback(
    void *context, BIO *bio, int operation, const void *argument,
    size_t argument_length, int argument_integer, long argument_long,
    int result, size_t *processed_out) {
  openssl_mmsg_test_context *test_context;
  (void)argument_integer;
  (void)argument_long;
  (void)processed_out;
  test_context = (openssl_mmsg_test_context *)context;
  if (test_context == 0 || bio != cpkt_openssl_BIO_native(test_context->bio) ||
      (operation & ~BIO_CB_RETURN) != BIO_CB_WRITE || argument == 0 ||
      argument_length != 3U || memcmp(argument, "abc", 3U) != 0)
    return 0L;
  ++test_context->regular_ex_callback_calls;
  return (long)result;
}

static int openssl_mmsg_test_callback(void *context, BIO *bio,
                                      cpkt_openssl_bio_message *messages,
                                      size_t message_stride,
                                      size_t message_count,
                                      cpkt_openssl_u64 flags,
                                      size_t *processed_out) {
  openssl_mmsg_test_context *test_context;

  test_context = (openssl_mmsg_test_context *)context;
  if (test_context == 0 || bio != cpkt_openssl_BIO_native(test_context->bio) ||
      messages == 0 || message_stride != sizeof(*messages) ||
      !openssl_mmsg_test_messages(messages, message_count) ||
      cpkt_openssl_u64_high_word(flags) != 0UL ||
      cpkt_openssl_u64_low_word(flags) != 0UL || processed_out == 0) {
    return 0;
  }
  ++test_context->calls;
  test_context->close_result = cpkt_openssl_BIO_close(test_context->bio);
  *processed_out = message_count;
  return 1;
}

static long openssl_mmsg_test_legacy_callback(void *context, BIO *bio,
                                              int operation,
                                              const void *argument,
                                              int argument_integer,
                                              long argument_long, long result) {
  openssl_mmsg_test_context *test_context;
  const cpkt_openssl_bio_mmsg_callback_args *arguments;

  test_context = (openssl_mmsg_test_context *)context;
  arguments = (const cpkt_openssl_bio_mmsg_callback_args *)argument;
  if (test_context == 0 || bio != cpkt_openssl_BIO_native(test_context->bio) ||
      (operation & BIO_CB_SENDMMSG) == 0 || arguments == 0 ||
      arguments->messages == 0 ||
      arguments->message_stride != sizeof(*arguments->messages) ||
      !openssl_mmsg_test_messages(arguments->messages,
                                  arguments->message_count) ||
      cpkt_openssl_u64_high_word(arguments->flags) != 0UL ||
      cpkt_openssl_u64_low_word(arguments->flags) != 0UL ||
      arguments->processed_out == 0 || argument_integer != 0 ||
      argument_long != 0L || result < 0L) {
    return 0L;
  }
  ++test_context->legacy_callback_calls;
  return 1L;
}

static long openssl_mmsg_test_extended_callback(
    void *context, BIO *bio, int operation, const void *argument,
    size_t argument_length, int argument_integer, long argument_long,
    int result, size_t *processed_out) {
  openssl_mmsg_test_context *test_context;
  const cpkt_openssl_bio_mmsg_callback_args *arguments;

  test_context = (openssl_mmsg_test_context *)context;
  arguments = (const cpkt_openssl_bio_mmsg_callback_args *)argument;
  (void)argument_length;
  if (test_context == 0 || bio != cpkt_openssl_BIO_native(test_context->bio) ||
      (operation & BIO_CB_RECVMMSG) == 0 || arguments == 0 ||
      arguments->messages == 0 ||
      arguments->message_stride != sizeof(*arguments->messages) ||
      !openssl_mmsg_test_messages(arguments->messages,
                                  arguments->message_count) ||
      cpkt_openssl_u64_high_word(arguments->flags) != 0UL ||
      cpkt_openssl_u64_low_word(arguments->flags) != 0UL ||
      arguments->processed_out == 0 || argument_integer != 0 ||
      argument_long != 0L || result < 0 ||
      (processed_out != 0 && processed_out != arguments->processed_out)) {
    return 0L;
  }
  ++test_context->extended_callback_calls;
  return 1L;
}

int main(void) {
  ASN1_ENUMERATED *enumerated;
  ASN1_INTEGER *integer;
  CT_POLICY_EVAL_CTX *policy_context;
  BIO *bio;
  BIO *dgram_left;
  BIO *dgram_right;
  BIO *ordinary_bio;
  BIO *ordinary_copy;
  BIO *retained_bio;
  OSSL_LIB_CTX *library_context;
  OSSL_PARAM *built_parameters;
  OSSL_PARAM *native_parameter;
  OSSL_PARAM_BLD *parameter_builder;
  SSL_CTX *context;
  SCT *sct;
  X509_ALGOR *algorithm;
  CRYPTO_RWLOCK *atomic_lock;
  cpkt_openssl_atomic_u64 *atomic_value;
  cpkt_openssl_sha512_context *sha_context;
  unsigned char digest[SHA512_DIGEST_LENGTH];
  unsigned char derived_key[16];
  unsigned char iv[16];
  unsigned char salt[4];
  char read_buffer[4];
  cpkt_openssl_i64 signed_value;
  cpkt_openssl_bio_message receive_message;
  cpkt_openssl_bio_message send_message;
  cpkt_openssl_bio *facade_bio;
  cpkt_openssl_bio *plain_facade_bio;
  cpkt_openssl_bio_method *facade_method;
  cpkt_openssl_bio_method *plain_facade_method;
  cpkt_openssl_param_i64 *signed_parameter;
  cpkt_openssl_poll_descriptor poll_descriptor;
  cpkt_openssl_param_u64 *unsigned_parameter;
  cpkt_openssl_ssl_poll_item poll_item;
  cpkt_openssl_u64 options;
  cpkt_openssl_u64 returned;
  openssl_padded_native_message padded_messages[2];
  size_t processed;
  int retained_mmsg_calls;
  openssl_mmsg_test_context mmsg_context;

  options = cpkt_openssl_u64_make(0UL, 0x4000UL);
  if (cpkt_openssl_u64_high_word(options) != 0UL ||
      cpkt_openssl_u64_low_word(options) != 0x4000UL ||
      cpkt_openssl_u64_is_zero(options) ||
      !cpkt_openssl_u64_equal(options, cpkt_openssl_u64_make(0UL, 0x4000UL))) {
    return 1;
  }
  if (!cpkt_openssl_u64_is_zero(cpkt_openssl_u64_make(0UL, 0UL))) {
    return 2;
  }
  atomic_lock = CRYPTO_THREAD_lock_new();
  atomic_value = cpkt_openssl_atomic_u64_new(cpkt_openssl_u64_make(0UL, 1UL));
  if (atomic_lock == 0 || atomic_value == 0 ||
      cpkt_openssl_CRYPTO_atomic_add64(atomic_value,
                                       cpkt_openssl_u64_make(0UL, 2UL),
                                       &returned, atomic_lock) != 1 ||
      !cpkt_openssl_u64_equal(returned, cpkt_openssl_u64_make(0UL, 3UL)) ||
      cpkt_openssl_CRYPTO_atomic_and(atomic_value,
                                     cpkt_openssl_u64_make(0UL, 1UL), &returned,
                                     atomic_lock) != 1 ||
      !cpkt_openssl_u64_equal(returned, cpkt_openssl_u64_make(0UL, 1UL)) ||
      cpkt_openssl_CRYPTO_atomic_or(atomic_value,
                                    cpkt_openssl_u64_make(0UL, 2UL), &returned,
                                    atomic_lock) != 1 ||
      !cpkt_openssl_u64_equal(returned, cpkt_openssl_u64_make(0UL, 3UL)) ||
      cpkt_openssl_CRYPTO_atomic_store(
          atomic_value, cpkt_openssl_u64_make(0UL, 5UL), atomic_lock) != 1 ||
      cpkt_openssl_CRYPTO_atomic_load(atomic_value, &returned, atomic_lock) !=
          1 ||
      !cpkt_openssl_u64_equal(returned, cpkt_openssl_u64_make(0UL, 5UL))) {
    cpkt_openssl_atomic_u64_free(atomic_value);
    CRYPTO_THREAD_lock_free(atomic_lock);
    return 3;
  }
  cpkt_openssl_atomic_u64_free(atomic_value);
  CRYPTO_THREAD_lock_free(atomic_lock);
  sha_context = cpkt_openssl_SHA512_CTX_new();
  if (sha_context == 0 || cpkt_openssl_SHA512_Init(sha_context) != 1 ||
      cpkt_openssl_SHA512_Update(sha_context, "abc", 3U) != 1 ||
      cpkt_openssl_SHA512_Final(digest, sha_context) != 1 ||
      digest[0] != 0xddU || cpkt_openssl_SHA384_Init(sha_context) != 1 ||
      cpkt_openssl_SHA384_Update(sha_context, "abc", 3U) != 1 ||
      cpkt_openssl_SHA384_Final(digest, sha_context) != 1 ||
      digest[0] != 0xcbU) {
    cpkt_openssl_SHA512_CTX_free(sha_context);
    return 4;
  }
  cpkt_openssl_SHA512_CTX_free(sha_context);
  bio = BIO_new(BIO_s_mem());
  if (bio == 0 || BIO_write(bio, "abc", 3) != 3 ||
      cpkt_openssl_u64_low_word(cpkt_openssl_BIO_number_written(bio)) != 3UL ||
      BIO_read(bio, read_buffer, 3) != 3 ||
      cpkt_openssl_u64_low_word(cpkt_openssl_BIO_number_read(bio)) != 3UL) {
    BIO_free(bio);
    return 5;
  }
  BIO_free(bio);
  dgram_left = 0;
  dgram_right = 0;
  memset(&send_message, 0, sizeof(send_message));
  memset(&receive_message, 0, sizeof(receive_message));
  send_message.data = "abc";
  send_message.data_length = 3U;
  receive_message.data = read_buffer;
  receive_message.data_length = sizeof(read_buffer) - 1U;
  processed = 0U;
  if (BIO_new_bio_dgram_pair(&dgram_left, 0U, &dgram_right, 0U) != 1) {
    return 23;
  }
  if (cpkt_openssl_BIO_sendmmsg(dgram_left, &send_message, sizeof(send_message),
                                1U, cpkt_openssl_u64_make(0UL, 0UL),
                                &processed) != 1) {
    BIO_free(dgram_left);
    BIO_free(dgram_right);
    return 24;
  }
  if (processed != 1U) {
    BIO_free(dgram_left);
    BIO_free(dgram_right);
    return 25;
  }
  if (cpkt_openssl_BIO_recvmmsg(
          dgram_right, &receive_message, sizeof(receive_message), 1U,
          cpkt_openssl_u64_make(0UL, 0UL), &processed) != 1) {
    BIO_free(dgram_left);
    BIO_free(dgram_right);
    return 26;
  }
  if (processed != 1U || receive_message.data_length != 3U ||
      memcmp(read_buffer, "abc", 3U) != 0) {
    BIO_free(dgram_left);
    BIO_free(dgram_right);
    return 27;
  }
  if (cpkt_openssl_BIO_get_rpoll_descriptor(dgram_left, &poll_descriptor) !=
      0) {
    BIO_free(dgram_left);
    BIO_free(dgram_right);
    return 28;
  }
  cpkt_openssl_SSL_as_poll_descriptor(0, &poll_descriptor);
  if (poll_descriptor.type != BIO_POLL_DESCRIPTOR_TYPE_SSL ||
      poll_descriptor.value_kind != CPKT_OPENSSL_POLL_VALUE_SSL ||
      poll_descriptor.ssl != 0) {
    BIO_free(dgram_left);
    BIO_free(dgram_right);
    return 24;
  }
  memset(&poll_item, 0, sizeof(poll_item));
  poll_item.descriptor.value_kind = 999UL;
  if (cpkt_openssl_SSL_poll(&poll_item, 1U, sizeof(poll_item), 0,
                            cpkt_openssl_u64_make(0UL, 0UL), &processed) != 0) {
    BIO_free(dgram_left);
    BIO_free(dgram_right);
    return 25;
  }
  BIO_free(dgram_left);
  BIO_free(dgram_right);
  facade_method = cpkt_openssl_BIO_meth_new(BIO_TYPE_NONE, "cpkt-mmsg");
  plain_facade_method = cpkt_openssl_BIO_meth_new(BIO_TYPE_NONE, "cpkt-plain");
  if (facade_method == 0 || plain_facade_method == 0 ||
      BIO_meth_set_create(
          (BIO_METHOD *)cpkt_openssl_BIO_meth_native(facade_method),
          openssl_mmsg_test_create) != 1 ||
      BIO_meth_set_write(
          (BIO_METHOD *)cpkt_openssl_BIO_meth_native(facade_method),
          openssl_mmsg_test_write) != 1 ||
      BIO_meth_set_create(
          (BIO_METHOD *)cpkt_openssl_BIO_meth_native(plain_facade_method),
          openssl_mmsg_test_create) != 1 ||
      BIO_meth_set_write(
          (BIO_METHOD *)cpkt_openssl_BIO_meth_native(plain_facade_method),
          openssl_mmsg_test_write) != 1 ||
      BIO_meth_set_destroy(
          (BIO_METHOD *)cpkt_openssl_BIO_meth_native(plain_facade_method),
          openssl_mmsg_test_destroy) != 1 ||
      BIO_meth_set_ctrl(
          (BIO_METHOD *)cpkt_openssl_BIO_meth_native(plain_facade_method),
          openssl_mmsg_test_ctrl) != 1 ||
      cpkt_openssl_BIO_meth_set_sendmmsg(plain_facade_method,
                                         openssl_retained_mmsg_callback) != 1 ||
      cpkt_openssl_BIO_meth_set_sendmmsg(facade_method,
                                         openssl_mmsg_test_callback) != 1 ||
      cpkt_openssl_BIO_meth_set_recvmmsg(facade_method,
                                         openssl_mmsg_test_callback) != 1 ||
      cpkt_openssl_BIO_meth_get_sendmmsg(facade_method) !=
          openssl_mmsg_test_callback ||
      cpkt_openssl_BIO_meth_get_recvmmsg(facade_method) !=
          openssl_mmsg_test_callback) {
    cpkt_openssl_BIO_meth_close(facade_method);
    cpkt_openssl_BIO_meth_close(plain_facade_method);
    return 30;
  }
  retained_mmsg_calls = 0;
  plain_facade_bio =
      cpkt_openssl_BIO_new(plain_facade_method, &retained_mmsg_calls);
  if (plain_facade_bio == 0 ||
      cpkt_openssl_BIO_meth_close(plain_facade_method) != 0) {
    cpkt_openssl_BIO_close(plain_facade_bio);
    cpkt_openssl_BIO_meth_close(plain_facade_method);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 31;
  }
  openssl_duplicate_source = cpkt_openssl_BIO_native(plain_facade_bio);
  cpkt_openssl_BIO_set_callback(plain_facade_bio, openssl_duplicate_callback);
  if (BIO_dup_chain(openssl_duplicate_source) != 0 ||
      openssl_mmsg_destroy_calls != 1 ||
      BIO_write(openssl_duplicate_source, "abc", 3) != 3) {
    cpkt_openssl_BIO_close(plain_facade_bio);
    cpkt_openssl_BIO_meth_close(plain_facade_method);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 43;
  }
  ordinary_bio = BIO_new(BIO_s_mem());
  ordinary_copy = ordinary_bio == 0 ? 0 : BIO_dup_chain(ordinary_bio);
  if (ordinary_copy == 0) {
    BIO_free(ordinary_bio);
    cpkt_openssl_BIO_close(plain_facade_bio);
    cpkt_openssl_BIO_meth_close(plain_facade_method);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 44;
  }
  BIO_free(ordinary_copy);
  BIO_free(ordinary_bio);
  retained_bio = cpkt_openssl_BIO_native(plain_facade_bio);
  if (BIO_up_ref(retained_bio) != 1) {
    cpkt_openssl_BIO_close(plain_facade_bio);
    cpkt_openssl_BIO_meth_close(plain_facade_method);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 31;
  }
  if (cpkt_openssl_BIO_close(plain_facade_bio) != 1) {
    BIO_free(retained_bio);
    cpkt_openssl_BIO_meth_close(plain_facade_method);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 31;
  }
  plain_facade_bio = 0;
  if (cpkt_openssl_BIO_meth_close(plain_facade_method) != 0 ||
      cpkt_openssl_BIO_sendmmsg(
          retained_bio, &send_message, sizeof(send_message), 1U,
          cpkt_openssl_u64_make(0UL, 0UL), &processed) != 1 ||
      processed != 1U || retained_mmsg_calls != 1 ||
      BIO_write(retained_bio, "abc", 3) != 3 || BIO_free(retained_bio) != 1 ||
      openssl_mmsg_destroy_calls != 2 ||
      cpkt_openssl_BIO_meth_close(plain_facade_method) != 1) {
    cpkt_openssl_BIO_meth_close(facade_method);
    return 31;
  }
  memset(&mmsg_context, 0, sizeof(mmsg_context));
  facade_bio = cpkt_openssl_BIO_new_ex(0, facade_method, &mmsg_context);
  mmsg_context.bio = facade_bio;
  cpkt_openssl_BIO_set_callback(facade_bio, openssl_mmsg_test_legacy_callback);
  if (facade_bio == 0 || cpkt_openssl_BIO_native(facade_bio) == 0 ||
      cpkt_openssl_BIO_get_callback(facade_bio) !=
          openssl_mmsg_test_legacy_callback) {
    cpkt_openssl_BIO_close(facade_bio);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 32;
  }
  if (cpkt_openssl_BIO_sendmmsg(cpkt_openssl_BIO_native(facade_bio),
                                &send_message, sizeof(send_message), 1U,
                                cpkt_openssl_u64_make(0UL, 0UL),
                                &processed) != 1) {
    cpkt_openssl_BIO_close(facade_bio);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 33;
  }
  if (mmsg_context.legacy_callback_calls != 2) {
    cpkt_openssl_BIO_close(facade_bio);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 34;
  }
  memset(padded_messages, 0, sizeof(padded_messages));
  padded_messages[0].message.data = "a";
  padded_messages[0].message.data_len = 1U;
  padded_messages[1].message.data = "bc";
  padded_messages[1].message.data_len = 2U;
  if (BIO_sendmmsg(cpkt_openssl_BIO_native(facade_bio),
                   &padded_messages[0].message, sizeof(padded_messages[0]), 2U,
                   0U, &processed) != 1) {
    cpkt_openssl_BIO_close(facade_bio);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 34;
  }
  if (mmsg_context.calls != 2) {
    cpkt_openssl_BIO_close(facade_bio);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 39;
  }
  if (mmsg_context.legacy_callback_calls != 4) {
    cpkt_openssl_BIO_close(facade_bio);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 40;
  }
  cpkt_openssl_BIO_set_callback(facade_bio, openssl_mmsg_test_regular_callback);
  if (BIO_write(cpkt_openssl_BIO_native(facade_bio), "abc", 3) != 3 ||
      mmsg_context.regular_callback_calls != 2) {
    cpkt_openssl_BIO_close(facade_bio);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 41;
  }
  cpkt_openssl_BIO_set_callback(facade_bio, 0);
  cpkt_openssl_BIO_set_callback_ex(facade_bio,
                                   openssl_mmsg_test_regular_ex_callback);
  if (BIO_write(cpkt_openssl_BIO_native(facade_bio), "abc", 3) != 3 ||
      mmsg_context.regular_ex_callback_calls != 2) {
    cpkt_openssl_BIO_close(facade_bio);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 42;
  }
  cpkt_openssl_BIO_set_callback_ex(facade_bio,
                                   openssl_mmsg_test_extended_callback);
  if (cpkt_openssl_BIO_get_callback(facade_bio) != 0 ||
      cpkt_openssl_BIO_get_callback_ex(facade_bio) !=
          openssl_mmsg_test_extended_callback) {
    cpkt_openssl_BIO_close(facade_bio);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 35;
  }
  if (cpkt_openssl_BIO_recvmmsg(cpkt_openssl_BIO_native(facade_bio),
                                &receive_message, sizeof(receive_message), 1U,
                                cpkt_openssl_u64_make(0UL, 0UL),
                                &processed) != 1) {
    cpkt_openssl_BIO_close(facade_bio);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 36;
  }
  if (mmsg_context.extended_callback_calls != 2 || mmsg_context.calls != 3 ||
      mmsg_context.close_result != 0) {
    cpkt_openssl_BIO_close(facade_bio);
    cpkt_openssl_BIO_meth_close(facade_method);
    return 37;
  }
  if (cpkt_openssl_BIO_close(facade_bio) != 1 ||
      cpkt_openssl_BIO_meth_close(facade_method) != 1) {
    return 38;
  }
  signed_value = cpkt_openssl_i64_make(0xffffffffUL, 0xfffffffeUL);
  if (cpkt_openssl_i64_high_word(signed_value) != 0xffffffffUL ||
      cpkt_openssl_i64_low_word(signed_value) != 0xfffffffeUL ||
      !cpkt_openssl_i64_equal(
          signed_value, cpkt_openssl_i64_make(0xffffffffUL, 0xfffffffeUL))) {
    return 6;
  }
  signed_parameter = 0;
  unsigned_parameter = 0;
  if (cpkt_openssl_OSSL_PARAM_construct_int64(&signed_parameter, "signed",
                                              signed_value) != 1 ||
      cpkt_openssl_OSSL_PARAM_construct_uint64(&unsigned_parameter, "unsigned",
                                               options) != 1 ||
      !cpkt_openssl_i64_equal(cpkt_openssl_param_i64_value(signed_parameter),
                              signed_value) ||
      !cpkt_openssl_u64_equal(cpkt_openssl_param_u64_value(unsigned_parameter),
                              options)) {
    cpkt_openssl_param_i64_free(signed_parameter);
    cpkt_openssl_param_u64_free(unsigned_parameter);
    return 7;
  }
  native_parameter = cpkt_openssl_param_i64_native(signed_parameter);
  if (cpkt_openssl_OSSL_PARAM_set_int64(native_parameter,
                                        cpkt_openssl_i64_make(0UL, 7UL)) != 1 ||
      cpkt_openssl_OSSL_PARAM_get_int64(&native_parameter[0], &signed_value) !=
          1 ||
      !cpkt_openssl_i64_equal(signed_value, cpkt_openssl_i64_make(0UL, 7UL))) {
    cpkt_openssl_param_i64_free(signed_parameter);
    cpkt_openssl_param_u64_free(unsigned_parameter);
    return 8;
  }
  native_parameter = cpkt_openssl_param_u64_native(unsigned_parameter);
  if (cpkt_openssl_OSSL_PARAM_set_uint64(
          native_parameter, cpkt_openssl_u64_make(0UL, 9UL)) != 1 ||
      cpkt_openssl_OSSL_PARAM_get_uint64(&native_parameter[0], &returned) !=
          1 ||
      !cpkt_openssl_u64_equal(returned, cpkt_openssl_u64_make(0UL, 9UL))) {
    cpkt_openssl_param_i64_free(signed_parameter);
    cpkt_openssl_param_u64_free(unsigned_parameter);
    return 9;
  }
  parameter_builder = OSSL_PARAM_BLD_new();
  if (parameter_builder == 0 ||
      cpkt_openssl_OSSL_PARAM_BLD_push_int64(
          parameter_builder, "signed", cpkt_openssl_i64_make(0UL, 11UL)) != 1 ||
      cpkt_openssl_OSSL_PARAM_BLD_push_uint64(
          parameter_builder, "unsigned", cpkt_openssl_u64_make(0UL, 12UL)) !=
          1) {
    OSSL_PARAM_BLD_free(parameter_builder);
    cpkt_openssl_param_i64_free(signed_parameter);
    cpkt_openssl_param_u64_free(unsigned_parameter);
    return 10;
  }
  built_parameters = OSSL_PARAM_BLD_to_param(parameter_builder);
  OSSL_PARAM_BLD_free(parameter_builder);
  if (built_parameters == 0 ||
      cpkt_openssl_OSSL_PARAM_get_int64(
          OSSL_PARAM_locate(built_parameters, "signed"), &signed_value) != 1 ||
      cpkt_openssl_OSSL_PARAM_get_uint64(
          OSSL_PARAM_locate(built_parameters, "unsigned"), &returned) != 1 ||
      !cpkt_openssl_i64_equal(signed_value, cpkt_openssl_i64_make(0UL, 11UL)) ||
      !cpkt_openssl_u64_equal(returned, cpkt_openssl_u64_make(0UL, 12UL))) {
    OSSL_PARAM_free(built_parameters);
    cpkt_openssl_param_i64_free(signed_parameter);
    cpkt_openssl_param_u64_free(unsigned_parameter);
    return 11;
  }
  OSSL_PARAM_free(built_parameters);
  cpkt_openssl_param_i64_free(signed_parameter);
  cpkt_openssl_param_u64_free(unsigned_parameter);
  signed_value = cpkt_openssl_i64_make(0xffffffffUL, 0xfffffffeUL);
  integer = ASN1_INTEGER_new();
  enumerated = ASN1_ENUMERATED_new();
  if (integer == 0 || enumerated == 0) {
    ASN1_INTEGER_free(integer);
    ASN1_ENUMERATED_free(enumerated);
    return 12;
  }
  if (cpkt_openssl_ASN1_INTEGER_set_int64(integer, signed_value) != 1 ||
      cpkt_openssl_ASN1_INTEGER_get_int64(&signed_value, integer) != 1 ||
      !cpkt_openssl_i64_equal(
          signed_value, cpkt_openssl_i64_make(0xffffffffUL, 0xfffffffeUL)) ||
      cpkt_openssl_ASN1_INTEGER_set_uint64(integer, options) != 1 ||
      cpkt_openssl_ASN1_INTEGER_get_uint64(&returned, integer) != 1 ||
      !cpkt_openssl_u64_equal(returned, options) ||
      cpkt_openssl_ASN1_ENUMERATED_set_int64(enumerated, signed_value) != 1 ||
      cpkt_openssl_ASN1_ENUMERATED_get_int64(&signed_value, enumerated) != 1 ||
      !cpkt_openssl_i64_equal(
          signed_value, cpkt_openssl_i64_make(0xffffffffUL, 0xfffffffeUL))) {
    ASN1_INTEGER_free(integer);
    ASN1_ENUMERATED_free(enumerated);
    return 13;
  }
  ASN1_INTEGER_free(integer);
  ASN1_ENUMERATED_free(enumerated);
  policy_context = CT_POLICY_EVAL_CTX_new();
  sct = SCT_new();
  library_context = OSSL_LIB_CTX_new();
  if (policy_context == 0 || sct == 0 || library_context == 0) {
    CT_POLICY_EVAL_CTX_free(policy_context);
    SCT_free(sct);
    OSSL_LIB_CTX_free(library_context);
    return 14;
  }
  cpkt_openssl_CT_POLICY_EVAL_CTX_set_time(policy_context, options);
  cpkt_openssl_SCT_set_timestamp(sct, options);
  if (!cpkt_openssl_u64_equal(
          cpkt_openssl_CT_POLICY_EVAL_CTX_get_time(policy_context), options) ||
      !cpkt_openssl_u64_equal(cpkt_openssl_SCT_get_timestamp(sct), options) ||
      cpkt_openssl_OSSL_set_max_threads(library_context,
                                        cpkt_openssl_u64_make(0UL, 1UL)) != 1 ||
      cpkt_openssl_u64_is_zero(
          cpkt_openssl_OSSL_get_max_threads(library_context))) {
    CT_POLICY_EVAL_CTX_free(policy_context);
    SCT_free(sct);
    OSSL_LIB_CTX_free(library_context);
    return 15;
  }
  cpkt_openssl_OSSL_sleep(cpkt_openssl_u64_make(0UL, 0UL));
  CT_POLICY_EVAL_CTX_free(policy_context);
  SCT_free(sct);
  OSSL_LIB_CTX_free(library_context);
  salt[0] = 1U;
  salt[1] = 2U;
  salt[2] = 3U;
  salt[3] = 4U;
  if (cpkt_openssl_EVP_PBE_scrypt(
          "password", 8U, salt, sizeof(salt), cpkt_openssl_u64_make(0UL, 16UL),
          cpkt_openssl_u64_make(0UL, 1UL), cpkt_openssl_u64_make(0UL, 1UL),
          cpkt_openssl_u64_make(0UL, 1048576UL), derived_key,
          sizeof(derived_key)) != 1) {
    return 16;
  }
  algorithm = cpkt_openssl_PKCS5_pbe2_set_scrypt(
      EVP_aes_128_cbc(), salt, (int)sizeof(salt), iv,
      cpkt_openssl_u64_make(0UL, 16UL), cpkt_openssl_u64_make(0UL, 1UL),
      cpkt_openssl_u64_make(0UL, 1UL));
  if (algorithm == 0) {
    return 17;
  }
  X509_ALGOR_free(algorithm);
  if (cpkt_openssl_OPENSSL_init_ssl(cpkt_openssl_u64_make(0UL, 0UL), 0) != 1) {
    return 18;
  }
  context = SSL_CTX_new(TLS_method());
  if (context == 0) {
    return 19;
  }
#if ULONG_MAX > 0xffffffffUL
  returned = cpkt_openssl_u64_make(0UL, 1UL);
  if (cpkt_openssl_SSL_get_value_uint(0, 0x100000000UL, 0UL, &returned) != 0 ||
      !cpkt_openssl_u64_is_zero(returned) ||
      cpkt_openssl_SSL_set_value_uint(0, 0UL, 0x100000000UL, options) != 0) {
    SSL_CTX_free(context);
    return 20;
  }
#endif
  returned = cpkt_openssl_SSL_CTX_set_options(context, options);
  if ((cpkt_openssl_u64_low_word(returned) & 0x4000UL) == 0UL) {
    SSL_CTX_free(context);
    return 21;
  }
  returned = cpkt_openssl_SSL_CTX_clear_options(context, options);
  SSL_CTX_free(context);
  return (cpkt_openssl_u64_low_word(returned) & 0x4000UL) == 0UL ? 0 : 22;
}
