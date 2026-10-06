#define _POSIX_C_SOURCE 200809L
#include <cpkt/openssl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *group;
static int callback_calls;
static int callback_result;
static int generic_calls;
static int generic_reject;
static int generic_edit_count;

static void check(int condition, const char *expression, int line) {
  if (!condition) {
    fprintf(stderr, "OpenSSL %s boundary, line %d: %s\n", group, line,
            expression);
    exit(EXIT_FAILURE);
  }
}
#define CHECK(expression) check((expression), #expression, __LINE__)

static const char inquiry_reason[] = "inquiry reason";

static void inquiry_scalar(SSL *native_ssl, SSL *facade_ssl, int operation) {
  cpkt_openssl_u64 value, original;
  uint64_t native_value;
  int native_result, facade_result;

  original = cpkt_openssl_u64_make(0x11223344UL, 37UL);
  value = original;
  native_value = ((uint64_t)0x11223344UL << 32) | 37UL;
  ERR_clear_error();
  if (operation == 0)
    native_result = SSL_get_handshake_rtt(native_ssl, &native_value);
  else if (operation == 1)
    native_result = SSL_get_stream_read_error_code(native_ssl, &native_value);
  else
    native_result = SSL_get_stream_write_error_code(native_ssl, &native_value);
  ERR_clear_error();
  if (operation == 0)
    facade_result = cpkt_openssl_SSL_get_handshake_rtt(facade_ssl, &value);
  else if (operation == 1)
    facade_result =
        cpkt_openssl_SSL_get_stream_read_error_code(facade_ssl, &value);
  else
    facade_result =
        cpkt_openssl_SSL_get_stream_write_error_code(facade_ssl, &value);
  CHECK(facade_result == native_result);
  CHECK(native_result == 0 || native_result == -1);
  CHECK(cpkt_openssl_u64_equal(value, original));
  ERR_clear_error();
}

static void inquiry_close(SSL *native_ssl, SSL *facade_ssl) {
  cpkt_openssl_ssl_conn_close_info value, original;
  SSL_CONN_CLOSE_INFO native_value;
  int native_result, facade_result;

  memset(&value, 0xa5, sizeof(value));
  value.error_code = cpkt_openssl_u64_make(0x11223344UL, 37UL);
  value.frame_type = cpkt_openssl_u64_make(0x55667788UL, 37UL);
  value.reason = inquiry_reason;
  value.reason_length = 37;
  value.flags = 37;
  memcpy(&original, &value, sizeof(value));
  memset(&native_value, 0, sizeof(native_value));
  native_value.error_code = ((uint64_t)0x11223344UL << 32) | 37UL;
  native_value.frame_type = ((uint64_t)0x55667788UL << 32) | 37UL;
  native_value.reason = inquiry_reason;
  native_value.reason_len = 37;
  native_value.flags = 37;
  ERR_clear_error();
  native_result =
      SSL_get_conn_close_info(native_ssl, &native_value, sizeof(native_value));
  ERR_clear_error();
  facade_result =
      cpkt_openssl_SSL_get_conn_close_info(facade_ssl, &value, sizeof(value));
  CHECK(facade_result == native_result);
  CHECK(native_result == 0 || native_result == -1);
  CHECK(memcmp(&value, &original, sizeof(value)) == 0);
  ERR_clear_error();
}

#ifdef CPKT_OPENSSL_INQUIRY_WRAP
static int inquiry_mock_enabled;
static int inquiry_mock_result;
static int inquiry_mock_write;
static int inquiry_mock_zero;
static int inquiry_mock_null;
static int inquiry_mock_operation;
static int inquiry_mock_calls;
static SSL *inquiry_mock_ssl;
static size_t inquiry_mock_length;

int __real_SSL_get_handshake_rtt(const SSL *ssl, uint64_t *value);
int __real_SSL_get_stream_read_error_code(SSL *ssl, uint64_t *value);
int __real_SSL_get_stream_write_error_code(SSL *ssl, uint64_t *value);
int __real_SSL_get_conn_close_info(SSL *ssl, SSL_CONN_CLOSE_INFO *value,
                                   size_t length);

static uint64_t inquiry_mock_value(void) {
  return inquiry_mock_zero ? 0 : ((uint64_t)0x12345678UL << 32) | 0x9abcdef0UL;
}

static int inquiry_mock_scalar(const SSL *ssl, uint64_t *value, int operation) {
  CHECK(ssl == inquiry_mock_ssl && operation == inquiry_mock_operation);
  CHECK((value == NULL) == inquiry_mock_null);
  ++inquiry_mock_calls;
  if (value != NULL && inquiry_mock_write)
    *value = inquiry_mock_value();
  return inquiry_mock_result;
}

int __wrap_SSL_get_handshake_rtt(const SSL *ssl, uint64_t *value) {
  if (!inquiry_mock_enabled)
    return __real_SSL_get_handshake_rtt(ssl, value);
  return inquiry_mock_scalar(ssl, value, 0);
}

int __wrap_SSL_get_stream_read_error_code(SSL *ssl, uint64_t *value) {
  if (!inquiry_mock_enabled)
    return __real_SSL_get_stream_read_error_code(ssl, value);
  return inquiry_mock_scalar(ssl, value, 1);
}

int __wrap_SSL_get_stream_write_error_code(SSL *ssl, uint64_t *value) {
  if (!inquiry_mock_enabled)
    return __real_SSL_get_stream_write_error_code(ssl, value);
  return inquiry_mock_scalar(ssl, value, 2);
}

int __wrap_SSL_get_conn_close_info(SSL *ssl, SSL_CONN_CLOSE_INFO *value,
                                   size_t length) {
  if (!inquiry_mock_enabled)
    return __real_SSL_get_conn_close_info(ssl, value, length);
  CHECK(ssl == inquiry_mock_ssl && inquiry_mock_operation == 3);
  CHECK((value == NULL) == inquiry_mock_null && length == inquiry_mock_length);
  ++inquiry_mock_calls;
  if (value != NULL && inquiry_mock_write) {
    value->error_code = inquiry_mock_value();
    value->frame_type = inquiry_mock_value();
    value->reason = inquiry_mock_zero ? NULL : inquiry_reason;
    value->reason_len = inquiry_mock_zero ? 0 : sizeof(inquiry_reason) - 1;
    value->flags = inquiry_mock_zero ? 0 : 3;
  }
  return inquiry_mock_result;
}

static void inquiry_wrapped(SSL *ssl) {
  cpkt_openssl_u64 scalar, expected;
  cpkt_openssl_ssl_conn_close_info close_info, original;
  int results[3], status, write, zero, null_output, operation, result;

  results[0] = -1;
  results[1] = 0;
  results[2] = 1;
  inquiry_mock_ssl = ssl;
  inquiry_mock_enabled = 1;
  for (status = 0; status < 3; ++status)
    for (write = 0; write < (results[status] == 1 ? 1 : 2); ++write)
      for (zero = 0; zero < 2; ++zero)
        for (null_output = 0; null_output < 2; ++null_output)
          for (operation = 0; operation < 4; ++operation) {
            inquiry_mock_result = results[status];
            inquiry_mock_write = write || results[status] == 1;
            inquiry_mock_zero = zero;
            inquiry_mock_null = null_output;
            inquiry_mock_operation = operation;
            inquiry_mock_calls = 0;
            scalar = cpkt_openssl_u64_make(0x11223344UL, 37UL);
            memset(&close_info, 0xa5, sizeof(close_info));
            close_info.error_code = scalar;
            close_info.frame_type = scalar;
            close_info.reason = inquiry_reason;
            close_info.reason_length = 37;
            close_info.flags = 37;
            memcpy(&original, &close_info, sizeof(close_info));
            inquiry_mock_length = null_output ? 7 : sizeof(SSL_CONN_CLOSE_INFO);
            if (operation == 0)
              result = cpkt_openssl_SSL_get_handshake_rtt(
                  ssl, null_output ? NULL : &scalar);
            else if (operation == 1)
              result = cpkt_openssl_SSL_get_stream_read_error_code(
                  ssl, null_output ? NULL : &scalar);
            else if (operation == 2)
              result = cpkt_openssl_SSL_get_stream_write_error_code(
                  ssl, null_output ? NULL : &scalar);
            else
              result = cpkt_openssl_SSL_get_conn_close_info(
                  ssl, null_output ? NULL : &close_info,
                  null_output ? 7 : sizeof(close_info));
            CHECK(result == results[status] && inquiry_mock_calls == 1);
            expected = cpkt_openssl_u64_make(0x11223344UL, 37UL);
            if (result == 1 && !null_output)
              expected =
                  zero ? cpkt_openssl_u64_make(0, 0)
                       : cpkt_openssl_u64_make(0x12345678UL, 0x9abcdef0UL);
            CHECK(cpkt_openssl_u64_equal(
                scalar, operation == 3 ? original.error_code : expected));
            if (operation == 3 && result == 1 && !null_output) {
              CHECK(cpkt_openssl_u64_equal(close_info.error_code, expected));
              CHECK(cpkt_openssl_u64_equal(close_info.frame_type, expected));
              CHECK(close_info.reason == (zero ? NULL : inquiry_reason));
              CHECK(close_info.reason_length ==
                    (zero ? 0 : sizeof(inquiry_reason) - 1));
              CHECK(close_info.flags == (zero ? 0UL : 3UL));
            } else {
              CHECK(memcmp(&close_info, &original, sizeof(close_info)) == 0);
            }
          }
  inquiry_mock_enabled = 0;
}
#endif

static void inquiry_results(void) {
  SSL_CTX *context;
  SSL *native_ssl, *facade_ssl;
  int kind, operation;

  for (kind = 0; kind < 3; ++kind) {
    context = SSL_CTX_new(kind == 0 ? TLS_method() : OSSL_QUIC_client_method());
    CHECK(context != NULL);
    native_ssl = kind == 2 ? SSL_new_domain(context, 0) : SSL_new(context);
    facade_ssl = kind == 2 ? SSL_new_domain(context, 0) : SSL_new(context);
    CHECK(native_ssl != NULL && facade_ssl != NULL);
    for (operation = 0; operation < 3; ++operation)
      inquiry_scalar(native_ssl, facade_ssl, operation);
    inquiry_close(native_ssl, facade_ssl);
#ifdef CPKT_OPENSSL_INQUIRY_WRAP
    if (kind == 0)
      inquiry_wrapped(facade_ssl);
#endif
    SSL_free(facade_ssl);
    SSL_free(native_ssl);
    SSL_CTX_free(context);
  }
}

/* Native records are used only as independent upstream peers. The facade
 * half of every comparison uses C89 word values and public facade records. */
static void poll_empty(void) {
  cpkt_openssl_ssl_poll_item item, original;
  SSL_POLL_ITEM native_item;
  struct timeval timeout;
  const struct timeval *timeout_pointer;
  size_t strides[5], native_count, facade_count;
  int pointer_case, stride_case, timeout_case, flag_case, count_case;
  int native_result, facade_result;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  strides[0] = 0;
  strides[1] = 1;
  strides[2] = sizeof(item) - 1;
  strides[3] = sizeof(item);
  strides[4] = (size_t)-1;
  memset(&item, 0xa5, sizeof(item));
  memcpy(&original, &item, sizeof(item));
  memset(&native_item, 0, sizeof(native_item));
  for (pointer_case = 0; pointer_case < 2; ++pointer_case)
    for (stride_case = 0; stride_case < 5; ++stride_case)
      for (timeout_case = 0; timeout_case < 2; ++timeout_case)
        for (flag_case = 0; flag_case < 2; ++flag_case)
          for (count_case = 0; count_case < 2; ++count_case) {
            timeout_pointer = timeout_case ? &timeout : NULL;
            native_count = facade_count = 123;
            native_item.events = flag_case;
            native_item.events <<= 32;
            if (flag_case)
              native_item.events |= SSL_POLL_FLAG_NO_HANDLE_EVENTS;
            ERR_clear_error();
            native_result =
                SSL_poll(pointer_case ? &native_item : NULL, 0,
                         strides[stride_case], timeout_pointer,
                         native_item.events, count_case ? &native_count : NULL);
            CHECK(ERR_peek_last_error() == 0);
            facade_result = cpkt_openssl_SSL_poll(
                pointer_case ? &item : NULL, 0, strides[stride_case],
                timeout_pointer,
                cpkt_openssl_u64_make((unsigned long)flag_case,
                                      flag_case ? SSL_POLL_FLAG_NO_HANDLE_EVENTS
                                                : 0),
                count_case ? &facade_count : NULL);
            CHECK(native_result == 1 && facade_result == native_result);
            CHECK(native_count == facade_count);
            CHECK(facade_count == (count_case ? 0U : 123U));
            CHECK(memcmp(&item, &original, sizeof(item)) == 0);
            CHECK(ERR_peek_last_error() == 0);
          }
}

static double monotonic_seconds(void) {
  struct timespec now;
  CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
  return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static void poll_timeout(void) {
  struct timeval timeout;
  size_t count;
  double start, elapsed;
  int linkage;
  timeout.tv_sec = 0;
  timeout.tv_usec = 40000;
  for (linkage = 0; linkage < 2; ++linkage) {
    count = 123;
    start = monotonic_seconds();
    if (linkage)
      CHECK(cpkt_openssl_SSL_poll(NULL, 0, 0, &timeout,
                                  cpkt_openssl_u64_make(0, 0), &count) == 1);
    else
      CHECK(SSL_poll(NULL, 0, 0, &timeout, 0, &count) == 1);
    elapsed = monotonic_seconds() - start;
    /* Allow millisecond rounding; no upper wall-time assertion on busy hosts.
     * CTest's timeout bounds accidental infinite waits. */
    CHECK(elapsed >= 0.03);
    CHECK(count == 0);
    CHECK(timeout.tv_sec == 0 && timeout.tv_usec == 40000);
  }
}

typedef struct padded_poll_item {
  cpkt_openssl_ssl_poll_item item;
  unsigned char padding[32];
} padded_poll_item;

static void poll_records(void) {
  padded_poll_item items[3];
  SSL_POLL_ITEM native_items[3];
  struct timeval timeout;
  SSL_CTX *context;
  SSL *ssl;
  size_t native_count, facade_count;
  unsigned long native_error, facade_error;
  int scenario, index, native_result, facade_result;
  context = SSL_CTX_new(TLS_method());
  CHECK(context != NULL);
  ssl = SSL_new(context);
  CHECK(ssl != NULL);
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  for (scenario = 0; scenario < 4; ++scenario) {
    memset(items, 0, sizeof(items));
    memset(native_items, 0, sizeof(native_items));
    for (index = 0; index < 3; ++index) {
      memset(items[index].padding, 0xa5, sizeof(items[index].padding));
      items[index].item.events = cpkt_openssl_u64_make(1, SSL_POLL_EVENT_R);
      items[index].item.returned_events = cpkt_openssl_u64_make(2, 123);
      native_items[index].events = 1;
      native_items[index].events <<= 32;
      native_items[index].events |= SSL_POLL_EVENT_R;
      native_items[index].revents = 2;
      native_items[index].revents <<= 32;
      native_items[index].revents |= 123;
      items[index].item.descriptor.type = BIO_POLL_DESCRIPTOR_TYPE_SSL;
      items[index].item.descriptor.value_kind = CPKT_OPENSSL_POLL_VALUE_SSL;
      native_items[index].desc.type = BIO_POLL_DESCRIPTOR_TYPE_SSL;
    }
    if (scenario == 1) {
      items[1].item.descriptor.type = BIO_POLL_DESCRIPTOR_TYPE_NONE;
      items[1].item.descriptor.value_kind = CPKT_OPENSSL_POLL_VALUE_NONE;
      native_items[1].desc.type = BIO_POLL_DESCRIPTOR_TYPE_NONE;
    } else if (scenario == 3) {
      items[1].item.descriptor.type = BIO_POLL_DESCRIPTOR_TYPE_SSL;
      items[1].item.descriptor.value_kind = CPKT_OPENSSL_POLL_VALUE_SSL;
      items[1].item.descriptor.ssl = ssl;
      native_items[1].desc.type = BIO_POLL_DESCRIPTOR_TYPE_SSL;
      native_items[1].desc.value.ssl = ssl;
    } else if (scenario == 2) {
      items[1].item.descriptor.type = BIO_POLL_DESCRIPTOR_TYPE_SOCK_FD;
      items[1].item.descriptor.value_kind = CPKT_OPENSSL_POLL_VALUE_FD;
      items[1].item.descriptor.file_descriptor = -1;
      native_items[1].desc.type = BIO_POLL_DESCRIPTOR_TYPE_SOCK_FD;
      native_items[1].desc.value.fd = -1;
    }
    native_count = facade_count = 123;
    ERR_clear_error();
    native_result = SSL_poll(native_items, 3, sizeof(native_items[0]), &timeout,
                             0, &native_count);
    native_error = ERR_peek_last_error();
    ERR_clear_error();
    facade_result =
        cpkt_openssl_SSL_poll(&items[0].item, 3, sizeof(items[0]), &timeout,
                              cpkt_openssl_u64_make(0, 0), &facade_count);
    facade_error = ERR_peek_last_error();
    if (scenario == 0)
      CHECK(native_result == 1);
    CHECK(facade_result == native_result);
    CHECK(native_count == facade_count);
    CHECK(facade_count == (native_result ? 0U : 1U));
    CHECK(native_error == facade_error);
    for (index = 0; index < 3; ++index) {
      CHECK(cpkt_openssl_u64_high_word(items[index].item.returned_events) ==
            (unsigned long)(native_items[index].revents >> 32));
      CHECK(cpkt_openssl_u64_low_word(items[index].item.returned_events) ==
            (unsigned long)(native_items[index].revents & 0xffffffffUL));
      CHECK(cpkt_openssl_u64_equal(items[index].item.events,
                                   cpkt_openssl_u64_make(1, SSL_POLL_EVENT_R)));
      CHECK(items[index].item.descriptor.type == native_items[index].desc.type);
      if (scenario == 0 || scenario == 3)
        CHECK(items[1].item.descriptor.ssl == native_items[1].desc.value.ssl);
      if (scenario == 2)
        CHECK(items[1].item.descriptor.file_descriptor == -1);
      {
        size_t byte;
        for (byte = 0; byte < sizeof(items[index].padding); ++byte)
          CHECK(items[index].padding[byte] == 0xa5);
      }
    }
  }
  /* One record does not use its stride to reach another record. */
  native_count = facade_count = 123;
  CHECK(SSL_poll(native_items, 1, sizeof(native_items[0]), &timeout, 0,
                 &native_count) == 1);
  CHECK(cpkt_openssl_SSL_poll(&items[0].item, 1, (size_t)-1, &timeout,
                              cpkt_openssl_u64_make(0, 0), &facade_count) == 1);
  CHECK(native_count == facade_count && facade_count == 0);
  CHECK(ERR_peek_last_error() == facade_error);
  ERR_clear_error();
  SSL_free(ssl);
  SSL_CTX_free(context);
}

static void poll_invalid(void) {
  cpkt_openssl_ssl_poll_item item;
  struct timeval timeout;
  size_t count, maximum;
  maximum = (size_t)-1;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  memset(&item, 0, sizeof(item));
  count = 123;
  CHECK(cpkt_openssl_SSL_poll(NULL, 1, sizeof(item), &timeout,
                              cpkt_openssl_u64_make(0, 0), &count) == 0);
  CHECK(count == 123);
  CHECK(cpkt_openssl_SSL_poll(&item, 1, sizeof(item) - 1, &timeout,
                              cpkt_openssl_u64_make(0, 0), &count) == 0);
  CHECK(count == 123);
  CHECK(cpkt_openssl_SSL_poll(&item, maximum, sizeof(item), &timeout,
                              cpkt_openssl_u64_make(0, 0), &count) == 0);
  CHECK(count == 123);
  CHECK(cpkt_openssl_SSL_poll(&item, 2, maximum, &timeout,
                              cpkt_openssl_u64_make(0, 0), &count) == 0);
  CHECK(count == 123);
  item.descriptor.value_kind = 999;
  CHECK(cpkt_openssl_SSL_poll(&item, 1, sizeof(item), &timeout,
                              cpkt_openssl_u64_make(0, 0), &count) == 0);
  CHECK(count == 123);
}

static int empty_batch_callback(void *context, BIO *bio,
                                cpkt_openssl_bio_message *messages,
                                size_t stride, size_t count,
                                cpkt_openssl_u64 flags, size_t *processed) {
  (void)context;
  (void)bio;
  (void)messages;
  (void)stride;
  ++callback_calls;
  CHECK(count == 0);
  CHECK(cpkt_openssl_u64_equal(flags, cpkt_openssl_u64_make(1, 2)));
  *processed = 0;
  return callback_result;
}

static void mmsg_empty(void) {
  BIO *left, *right, *memory, *bio;
  cpkt_openssl_bio_method *method;
  cpkt_openssl_bio *shell;
  cpkt_openssl_bio_message unused, original;
  size_t native_count, facade_count;
  unsigned long native_error;
  int operation, bio_case, pointer_case, native_result, facade_result;
  left = right = NULL;
  CHECK(BIO_new_bio_dgram_pair(&left, 0, &right, 0) == 1);
  memory = BIO_new(BIO_s_mem());
  CHECK(memory != NULL);
  memset(&unused, 0xa5, sizeof(unused));
  memcpy(&original, &unused, sizeof(unused));
  for (operation = 0; operation < 2; ++operation)
    for (bio_case = 0; bio_case < 3; ++bio_case)
      for (pointer_case = 0; pointer_case < 2; ++pointer_case) {
        bio = bio_case == 0 ? left : (bio_case == 1 ? memory : NULL);
        native_count = facade_count = 123;
        ERR_clear_error();
        native_result = operation
                            ? BIO_sendmmsg(bio, NULL, 0, 0, 0, &native_count)
                            : BIO_recvmmsg(bio, NULL, 0, 0, 0, &native_count);
        native_error = ERR_peek_last_error();
        ERR_clear_error();
        if (operation)
          facade_result = cpkt_openssl_BIO_sendmmsg(
              bio, pointer_case ? &unused : NULL, pointer_case ? 1 : 0, 0,
              cpkt_openssl_u64_make(0, 0), &facade_count);
        else
          facade_result = cpkt_openssl_BIO_recvmmsg(
              bio, pointer_case ? &unused : NULL, pointer_case ? 1 : 0, 0,
              cpkt_openssl_u64_make(0, 0), &facade_count);
        CHECK(native_result == (bio_case == 0 ? 1 : 0));
        CHECK(facade_result == native_result);
        CHECK(native_count == facade_count && facade_count == 0);
        CHECK(ERR_peek_last_error() == native_error);
        CHECK(memcmp(&unused, &original, sizeof(unused)) == 0);
      }
  ERR_clear_error();
  BIO_free(memory);
  BIO_free(left);
  BIO_free(right);
  method = cpkt_openssl_BIO_meth_new(BIO_TYPE_NONE, "empty batch");
  CHECK(method != NULL);
  CHECK(cpkt_openssl_BIO_meth_set_sendmmsg(method, empty_batch_callback) == 1);
  CHECK(cpkt_openssl_BIO_meth_set_recvmmsg(method, empty_batch_callback) == 1);
  shell = cpkt_openssl_BIO_new(method, NULL);
  CHECK(shell != NULL);
  BIO_set_init(cpkt_openssl_BIO_native(shell), 1);
  for (operation = 0; operation < 2; ++operation)
    for (callback_result = 0; callback_result < 2; ++callback_result) {
      callback_calls = 0;
      facade_count = 123;
      if (operation)
        facade_result = cpkt_openssl_BIO_sendmmsg(
            cpkt_openssl_BIO_native(shell), NULL, 0, 0,
            cpkt_openssl_u64_make(1, 2), &facade_count);
      else
        facade_result = cpkt_openssl_BIO_recvmmsg(
            cpkt_openssl_BIO_native(shell), NULL, 0, 0,
            cpkt_openssl_u64_make(1, 2), &facade_count);
      CHECK(facade_result == callback_result);
      CHECK(facade_count == 0 && callback_calls == 1);
    }
  CHECK(cpkt_openssl_BIO_close(shell) == 1);
  CHECK(cpkt_openssl_BIO_meth_close(method) == 1);
}

static long empty_generic(int operation, const void *argument, long result,
                          int native) {
  int returning, bare_operation;
  size_t *processed;
  bare_operation = operation & ~BIO_CB_RETURN;
  if (bare_operation != BIO_CB_SENDMMSG && bare_operation != BIO_CB_RECVMMSG)
    return result;
  ++generic_calls;
  if (native) {
    const BIO_MMSG_CB_ARGS *args;
    args = (const BIO_MMSG_CB_ARGS *)argument;
    CHECK(args != NULL && args->num_msg == 0);
    CHECK((args->flags >> 32) == 1 && (args->flags & 0xffffffffUL) == 2);
    processed = args->msgs_processed;
  } else {
    const cpkt_openssl_bio_mmsg_callback_args *args;
    args = (const cpkt_openssl_bio_mmsg_callback_args *)argument;
    CHECK(args != NULL && args->message_count == 0);
    CHECK(cpkt_openssl_u64_equal(args->flags, cpkt_openssl_u64_make(1, 2)));
    processed = args->processed_out;
  }
  CHECK(processed != NULL);
  returning = (operation & BIO_CB_RETURN) != 0;
  if (returning && generic_edit_count)
    *processed = 7;
  if (generic_reject == (returning ? 2 : 1))
    return 0;
  return result;
}

static long native_legacy_callback(BIO *bio, int operation,
                                   const char *argument, int integer,
                                   long value, long result) {
  (void)bio;
  (void)integer;
  (void)value;
  return empty_generic(operation, argument, result, 1);
}

static long native_extended_callback(BIO *bio, int operation,
                                     const char *argument, size_t length,
                                     int integer, long value, int result,
                                     size_t *processed) {
  (void)length;
  (void)processed;
  return native_legacy_callback(bio, operation, argument, integer, value,
                                result);
}

static long facade_legacy_callback(void *context, BIO *bio, int operation,
                                   const void *argument, int integer,
                                   long value, long result) {
  (void)context;
  (void)bio;
  (void)integer;
  (void)value;
  return empty_generic(operation, argument, result, 0);
}

static long facade_extended_callback(void *context, BIO *bio, int operation,
                                     const void *argument, size_t length,
                                     int integer, long value, int result,
                                     size_t *processed) {
  (void)length;
  (void)processed;
  return facade_legacy_callback(context, bio, operation, argument, integer,
                                value, result);
}

/* The facade deliberately supports this deprecated upstream API. Keep its
 * native peer's deprecation diagnostic isolated from all other test code. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static void native_set_legacy(BIO *bio, BIO_callback_fn callback) {
  BIO_set_callback(bio, callback);
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

static int native_empty_batch(BIO *bio, BIO_MSG *messages, size_t stride,
                              size_t count, uint64_t flags, size_t *processed) {
  (void)bio;
  (void)messages;
  (void)stride;
  CHECK(count == 0);
  CHECK((flags >> 32) == 1 && (flags & 0xffffffffUL) == 2);
  ++callback_calls;
  *processed = 0;
  return callback_result;
}

static void mmsg_callbacks(void) {
  BIO_METHOD *native_method;
  BIO *native_bio;
  cpkt_openssl_bio_method *method;
  cpkt_openssl_bio *shell;
  uint64_t native_flags;
  size_t native_count, facade_count;
  int operation, kind, entry, native_result, facade_result;
  int native_generic_calls, native_batch_calls;
  native_method = BIO_meth_new(BIO_TYPE_NONE, "native empty peer");
  CHECK(native_method != NULL);
  CHECK(BIO_meth_set_sendmmsg(native_method, native_empty_batch) == 1);
  CHECK(BIO_meth_set_recvmmsg(native_method, native_empty_batch) == 1);
  native_bio = BIO_new(native_method);
  CHECK(native_bio != NULL);
  method = cpkt_openssl_BIO_meth_new(BIO_TYPE_NONE, "facade empty peer");
  CHECK(method != NULL);
  CHECK(cpkt_openssl_BIO_meth_set_sendmmsg(method, empty_batch_callback) == 1);
  CHECK(cpkt_openssl_BIO_meth_set_recvmmsg(method, empty_batch_callback) == 1);
  shell = cpkt_openssl_BIO_new(method, NULL);
  CHECK(shell != NULL);
  native_flags = 1;
  native_flags <<= 32;
  native_flags |= 2;
  for (operation = 0; operation < 2; ++operation)
    for (kind = 0; kind < 2; ++kind)
      for (callback_result = 0; callback_result < 2; ++callback_result)
        for (generic_reject = 0; generic_reject < 3; ++generic_reject)
          for (generic_edit_count = 0; generic_edit_count < 2;
               ++generic_edit_count)
            for (entry = 0; entry < 2; ++entry) {
              native_set_legacy(native_bio, NULL);
              BIO_set_callback_ex(native_bio, NULL);
              cpkt_openssl_BIO_set_callback(shell, NULL);
              cpkt_openssl_BIO_set_callback_ex(shell, NULL);
              if (kind) {
                BIO_set_callback_ex(native_bio, native_extended_callback);
                cpkt_openssl_BIO_set_callback_ex(shell,
                                                 facade_extended_callback);
              } else {
                native_set_legacy(native_bio, native_legacy_callback);
                cpkt_openssl_BIO_set_callback(shell, facade_legacy_callback);
              }
              callback_calls = generic_calls = 0;
              native_count = facade_count = 123;
              native_result = operation
                                  ? BIO_sendmmsg(native_bio, NULL, 0, 0,
                                                 native_flags, &native_count)
                                  : BIO_recvmmsg(native_bio, NULL, 0, 0,
                                                 native_flags, &native_count);
              native_generic_calls = generic_calls;
              native_batch_calls = callback_calls;
              CHECK(native_generic_calls == (generic_reject == 1 ? 1 : 2));
              CHECK(native_batch_calls == (generic_reject == 1 ? 0 : 1));
              callback_calls = generic_calls = 0;
              if (entry)
                facade_result =
                    operation ? cpkt_openssl_BIO_sendmmsg(
                                    cpkt_openssl_BIO_native(shell), NULL, 0, 0,
                                    cpkt_openssl_u64_make(1, 2), &facade_count)
                              : cpkt_openssl_BIO_recvmmsg(
                                    cpkt_openssl_BIO_native(shell), NULL, 0, 0,
                                    cpkt_openssl_u64_make(1, 2), &facade_count);
              else
                facade_result =
                    operation
                        ? BIO_sendmmsg(cpkt_openssl_BIO_native(shell), NULL, 0,
                                       0, native_flags, &facade_count)
                        : BIO_recvmmsg(cpkt_openssl_BIO_native(shell), NULL, 0,
                                       0, native_flags, &facade_count);
              CHECK(facade_result == native_result &&
                    facade_count == native_count);
              CHECK(generic_calls == native_generic_calls);
              CHECK(callback_calls == native_batch_calls);
            }
  CHECK(cpkt_openssl_BIO_close(shell) == 1);
  CHECK(cpkt_openssl_BIO_meth_close(method) == 1);
  BIO_free(native_bio);
  BIO_meth_free(native_method);
}

static void mmsg_invalid(void) {
  cpkt_openssl_bio_message message;
  BIO_MSG native_message;
  cpkt_openssl_bio_method *method;
  cpkt_openssl_bio *shell;
  size_t count, maximum;
  int operation, scenario, result;
  maximum = (size_t)-1;
  memset(&message, 0, sizeof(message));
  for (operation = 0; operation < 2; ++operation)
    for (scenario = 0; scenario < 4; ++scenario) {
      cpkt_openssl_bio_message *pointer;
      size_t stride, length;
      pointer = scenario == 0 ? NULL : &message;
      stride = scenario == 1 ? sizeof(message) - 1 : sizeof(message);
      length = scenario == 2 ? maximum : 1;
      if (scenario == 3) {
        length = 2;
        stride = maximum;
      }
      count = 123;
      ERR_clear_error();
      if (operation)
        result = cpkt_openssl_BIO_sendmmsg(NULL, pointer, stride, length,
                                           cpkt_openssl_u64_make(0, 0), &count);
      else
        result = cpkt_openssl_BIO_recvmmsg(NULL, pointer, stride, length,
                                           cpkt_openssl_u64_make(0, 0), &count);
      CHECK(result == 0 && count == 123);
      CHECK(ERR_peek_last_error() == 0);
    }
  method = cpkt_openssl_BIO_meth_new(BIO_TYPE_NONE, "invalid native span");
  CHECK(method != NULL);
  CHECK(cpkt_openssl_BIO_meth_set_sendmmsg(method, empty_batch_callback) == 1);
  CHECK(cpkt_openssl_BIO_meth_set_recvmmsg(method, empty_batch_callback) == 1);
  shell = cpkt_openssl_BIO_new(method, NULL);
  CHECK(shell != NULL);
  memset(&native_message, 0, sizeof(native_message));
  /* Native callers can reach both facade callback converters directly. An
   * impossible span must fail before reading records or invoking user code. */
  for (scenario = 0; scenario < 2; ++scenario) {
    cpkt_openssl_BIO_set_callback_ex(shell, scenario ? facade_extended_callback
                                                     : NULL);
    for (operation = 0; operation < 2; ++operation) {
      count = 123;
      callback_calls = generic_calls = 0;
      result = operation ? BIO_sendmmsg(cpkt_openssl_BIO_native(shell),
                                        &native_message, maximum, 2, 0, &count)
                         : BIO_recvmmsg(cpkt_openssl_BIO_native(shell),
                                        &native_message, maximum, 2, 0, &count);
      CHECK(result == 0 && count == 123);
      CHECK(callback_calls == 0 && generic_calls == 0);
    }
  }
  CHECK(cpkt_openssl_BIO_close(shell) == 1);
  CHECK(cpkt_openssl_BIO_meth_close(method) == 1);
}

int main(int argc, char **argv) {
  group = "arguments";
  CHECK(argc == 2);
  group = argv[1];
  if (strcmp(group, "poll_empty") == 0)
    poll_empty();
  else if (strcmp(group, "poll_timeout") == 0)
    poll_timeout();
  else if (strcmp(group, "poll_records") == 0)
    poll_records();
  else if (strcmp(group, "poll_invalid") == 0)
    poll_invalid();
  else if (strcmp(group, "mmsg_empty") == 0)
    mmsg_empty();
  else if (strcmp(group, "mmsg_invalid") == 0)
    mmsg_invalid();
  else if (strcmp(group, "mmsg_callbacks") == 0)
    mmsg_callbacks();
  else if (strcmp(group, "inquiry_results") == 0)
    inquiry_results();
  else
    CHECK(0);
  return EXIT_SUCCESS;
}
