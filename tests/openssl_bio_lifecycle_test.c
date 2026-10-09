#include <cpkt/openssl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum creation_mode { CREATE_SUCCESS, CREATE_REJECT, CREATE_EX_DATA_FAILURE };

static cpkt_openssl_bio_method *method;
static cpkt_openssl_bio *nested;
static OSSL_LIB_CTX *library_context;
static enum creation_mode mode;
static int creation_depth;
static int create_calls;
static int destroy_calls;
static int nested_on_destroy;
static int fail_next_allocation;
static int allocation_failures;

static void check(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "BIO lifecycle: %s (mode=%d)\n", message, (int)mode);
    exit(EXIT_FAILURE);
  }
}

/* OpenSSL's public allocator hooks inject the first ex-data publication
 * allocation failure after native creation. Facade allocations remain real. */
static void *test_malloc(size_t size, const char *file, int line) {
  (void)file;
  (void)line;
  if (fail_next_allocation) {
    fail_next_allocation = 0;
    ++allocation_failures;
    return NULL;
  }
  return malloc(size);
}

static void *test_realloc(void *pointer, size_t size, const char *file,
                          int line) {
  (void)file;
  (void)line;
  if (fail_next_allocation) {
    fail_next_allocation = 0;
    ++allocation_failures;
    return NULL;
  }
  return realloc(pointer, size);
}

static void test_free(void *pointer, const char *file, int line) {
  (void)file;
  (void)line;
  free(pointer);
}

static int batch_callback(void *context, BIO *bio,
                          cpkt_openssl_bio_message *messages, size_t stride,
                          size_t count, cpkt_openssl_u64 flags,
                          size_t *processed) {
  (void)context;
  (void)bio;
  (void)messages;
  (void)stride;
  (void)flags;
  *processed = count;
  return 1;
}

static void check_method_access(void) {
  check(cpkt_openssl_BIO_meth_get_sendmmsg(method) == batch_callback,
        "send callback inaccessible during native callback");
  check(cpkt_openssl_BIO_meth_get_recvmmsg(method) == batch_callback,
        "receive callback inaccessible during native callback");
  check(cpkt_openssl_BIO_meth_set_sendmmsg(method, batch_callback) == 1,
        "send callback registration failed during native callback");
  check(cpkt_openssl_BIO_meth_set_recvmmsg(method, batch_callback) == 1,
        "receive callback registration failed during native callback");
  check(cpkt_openssl_BIO_meth_close(method) == 0,
        "method freed while native construction/destruction was active");
}

static int create_callback(BIO *bio) {
  ++create_calls;
  ++creation_depth;
  check_method_access();
  BIO_set_init(bio, 1);
  if (creation_depth == 1) {
    nested = cpkt_openssl_BIO_new_ex(library_context, method, NULL);
    check(nested != NULL, "nested creation failed");
    --creation_depth;
    if (mode == CREATE_REJECT)
      return 0;
    if (mode == CREATE_EX_DATA_FAILURE)
      fail_next_allocation = 1;
  } else {
    --creation_depth;
  }
  return 1;
}

static int destroy_callback(BIO *bio) {
  cpkt_openssl_bio *temporary;
  (void)bio;
  ++destroy_calls;
  check_method_access();
  if (nested_on_destroy) {
    nested_on_destroy = 0;
    /* Avoid creating another retained child from this nested constructor. */
    ++creation_depth;
    temporary = cpkt_openssl_BIO_new(method, NULL);
    --creation_depth;
    check(temporary != NULL, "nested creation during destruction failed");
    check(cpkt_openssl_BIO_close(temporary) == 1, "nested destruction failed");
  }
  return 1;
}

static void run_case(enum creation_mode selected_mode, int explicit_context) {
  cpkt_openssl_bio *outer;
  BIO *retained;
  BIO_METHOD *native_method;
  mode = selected_mode;
  create_calls = 0;
  destroy_calls = 0;
  creation_depth = 0;
  allocation_failures = 0;
  method = cpkt_openssl_BIO_meth_new(BIO_TYPE_NONE, "reentrant-create");
  check(method != NULL, "method allocation failed");
  native_method = (BIO_METHOD *)cpkt_openssl_BIO_meth_native(method);
  check(BIO_meth_set_create(native_method, create_callback) == 1 &&
            BIO_meth_set_destroy(native_method, destroy_callback) == 1 &&
            cpkt_openssl_BIO_meth_set_sendmmsg(method, batch_callback) == 1 &&
            cpkt_openssl_BIO_meth_set_recvmmsg(method, batch_callback) == 1,
        "method configuration failed");
  outer = explicit_context
              ? cpkt_openssl_BIO_new_ex(library_context, method, NULL)
              : cpkt_openssl_BIO_new(method, NULL);
  check(create_calls == 2, "unexpected native create count");
  check((outer != NULL) == (mode == CREATE_SUCCESS),
        "unexpected constructor result");
  check(allocation_failures == (mode == CREATE_EX_DATA_FAILURE),
        "ex-data allocation failure was not exercised");
  check(destroy_calls == (mode == CREATE_EX_DATA_FAILURE),
        "failed creation used the wrong native cleanup path");
  check(cpkt_openssl_BIO_meth_close(method) == 0,
        "failed constructor released the retained child's method pin");
  if (outer != NULL) {
    /* Retained native ownership must continue to pin the method after close. */
    retained = cpkt_openssl_BIO_native(outer);
    check(BIO_up_ref(retained) == 1, "native up-ref failed");
    check(cpkt_openssl_BIO_close(outer) == 1, "outer close failed");
    check(destroy_calls == 0, "outer close destroyed a retained native BIO");
    check(cpkt_openssl_BIO_close(nested) == 1, "child close failed");
    check(cpkt_openssl_BIO_meth_close(method) == 0,
          "retained native reference lost its method pin");
    nested_on_destroy = 1;
    check(BIO_free(retained) == 1, "final native close failed");
    check(create_calls == 3 && destroy_calls == 3,
          "reentrant native destruction was not exercised");
  } else {
    check(cpkt_openssl_BIO_close(nested) == 1, "child close failed");
  }
  check(cpkt_openssl_BIO_meth_close(method) == 1,
        "constructor failure or destruction leaked a method pin");
  ERR_clear_error();
}

typedef struct batch_state {
  char payload[4][2][8];
  BIO_ADDR *addresses[4];
  int receive;
  int abort_phase;
  int before_calls;
  int after_calls;
  int method_calls;
} batch_state;

typedef struct padded_message {
  cpkt_openssl_bio_message message;
  unsigned long padding[2];
} padded_message;

static void batch_metadata(batch_state *state,
                           cpkt_openssl_bio_message *messages, size_t stride,
                           size_t count, int stage, int write) {
  size_t index;
  cpkt_openssl_bio_message *message;
  for (index = 0; index < count; ++index) {
    message = (cpkt_openssl_bio_message *)((unsigned char *)messages +
                                           index * stride);
    if (write) {
      message->data = state->payload[stage][index];
      message->data_length = (size_t)stage + index + 1;
      message->peer = state->addresses[(stage + index) % 4];
      message->local = state->addresses[(stage + index + 1) % 4];
      message->flags =
          cpkt_openssl_u64_make(0x80000000UL + stage, 0xf0000000UL + index);
    } else {
      check(message->data == state->payload[stage][index] &&
                message->data_length == (size_t)stage + index + 1 &&
                message->peer == state->addresses[(stage + index) % 4] &&
                message->local == state->addresses[(stage + index + 1) % 4] &&
                cpkt_openssl_u64_high_word(message->flags) ==
                    0x80000000UL + stage &&
                cpkt_openssl_u64_low_word(message->flags) ==
                    0xf0000000UL + index,
            "batch-message metadata changes were lost");
    }
  }
}

static long mutate_batch(batch_state *state, int operation,
                         const void *argument, long result) {
  const cpkt_openssl_bio_mmsg_callback_args *args;
  int returning;
  if ((operation & ~BIO_CB_RETURN) != BIO_CB_SENDMMSG &&
      (operation & ~BIO_CB_RETURN) != BIO_CB_RECVMMSG)
    return result;
  check((operation & ~BIO_CB_RETURN) ==
            (state->receive ? BIO_CB_RECVMMSG : BIO_CB_SENDMMSG),
        "wrong batch callback operation");
  args = (const cpkt_openssl_bio_mmsg_callback_args *)argument;
  check(args != NULL && args->messages != NULL && args->message_count == 2 &&
            args->message_stride >= sizeof(*args->messages) &&
            args->processed_out != NULL &&
            cpkt_openssl_u64_high_word(args->flags) == 0xabcdef01UL &&
            cpkt_openssl_u64_low_word(args->flags) == 0x23456789UL,
        "generic batch arguments were not preserved");
  returning = (operation & BIO_CB_RETURN) != 0;
  if (returning)
    ++state->after_calls;
  else
    ++state->before_calls;
  batch_metadata(state, args->messages, args->message_stride,
                 args->message_count, returning ? 2 : 0, 0);
  batch_metadata(state, args->messages, args->message_stride,
                 args->message_count, returning ? 3 : 1, 1);
  *args->processed_out = 1;
  if ((!returning && state->abort_phase == 1) ||
      (returning && state->abort_phase == 2))
    return 0;
  return result;
}

static long mutate_batch_legacy(void *context, BIO *bio, int operation,
                                const void *argument, int integer_argument,
                                long long_argument, long result) {
  (void)bio;
  (void)integer_argument;
  (void)long_argument;
  return mutate_batch((batch_state *)context, operation, argument, result);
}

static long mutate_batch_extended(void *context, BIO *bio, int operation,
                                  const void *argument, size_t length,
                                  int integer_argument, long long_argument,
                                  int result, size_t *processed) {
  (void)bio;
  (void)length;
  (void)integer_argument;
  (void)long_argument;
  (void)processed;
  return mutate_batch((batch_state *)context, operation, argument, result);
}

static int mutated_batch_method(void *context, BIO *bio,
                                cpkt_openssl_bio_message *messages,
                                size_t stride, size_t count,
                                cpkt_openssl_u64 flags, size_t *processed) {
  batch_state *state;
  (void)bio;
  (void)flags;
  state = (batch_state *)context;
  ++state->method_calls;
  check(count == 2, "method message count changed");
  batch_metadata(state, messages, stride, count, 1, 0);
  batch_metadata(state, messages, stride, count, 2, 1);
  *processed = count;
  return state->abort_phase == 3 ? 0 : 1;
}

static void run_batch_case(int extended, int receive, int abort_phase) {
  batch_state state;
  padded_message messages[2];
  cpkt_openssl_bio_method *batch_method;
  cpkt_openssl_bio *bio;
  size_t processed;
  size_t expected_processed;
  int index;
  int result;
  memset(&state, 0, sizeof(state));
  memset(messages, 0, sizeof(messages));
  state.receive = receive;
  state.abort_phase = abort_phase;
  for (index = 0; index < 4; ++index) {
    state.addresses[index] = BIO_ADDR_new();
    check(state.addresses[index] != NULL, "batch address allocation failed");
  }
  batch_method = cpkt_openssl_BIO_meth_new(BIO_TYPE_NONE, "batch-metadata");
  check(batch_method != NULL &&
            cpkt_openssl_BIO_meth_set_sendmmsg(batch_method,
                                               mutated_batch_method) == 1 &&
            cpkt_openssl_BIO_meth_set_recvmmsg(batch_method,
                                               mutated_batch_method) == 1,
        "batch method configuration failed");
  bio = cpkt_openssl_BIO_new(batch_method, &state);
  check(bio != NULL, "batch BIO allocation failed");
  BIO_set_init(cpkt_openssl_BIO_native(bio), 1);
  if (extended)
    cpkt_openssl_BIO_set_callback_ex(bio, mutate_batch_extended);
  else
    cpkt_openssl_BIO_set_callback(bio, mutate_batch_legacy);
  batch_metadata(&state, &messages[0].message, sizeof(messages[0]), 2, 0, 1);
  messages[0].padding[0] = messages[1].padding[0] = 0x13579bdfUL;
  messages[0].padding[1] = messages[1].padding[1] = 0x2468ace0UL;
  processed = 0;
  if (receive)
    result = cpkt_openssl_BIO_recvmmsg(
        cpkt_openssl_BIO_native(bio), &messages[0].message, sizeof(messages[0]),
        2, cpkt_openssl_u64_make(0xabcdef01UL, 0x23456789UL), &processed);
  else
    result = cpkt_openssl_BIO_sendmmsg(
        cpkt_openssl_BIO_native(bio), &messages[0].message, sizeof(messages[0]),
        2, cpkt_openssl_u64_make(0xabcdef01UL, 0x23456789UL), &processed);
  /* The native legacy callback translates its successful return value into
   * the processed count; the extended callback keeps the explicit output. */
  expected_processed = !extended && abort_phase == 0 ? 2 : 1;
  if (result != (abort_phase == 0) || processed != expected_processed ||
      state.before_calls != 1 || state.method_calls != (abort_phase != 1) ||
      state.after_calls != (abort_phase != 1))
    fprintf(stderr,
            "batch extended=%d receive=%d abort=%d result=%d processed=%lu "
            "before=%d method=%d after=%d\n",
            extended, receive, abort_phase, result, (unsigned long)processed,
            state.before_calls, state.method_calls, state.after_calls);
  check(result == (abort_phase == 0) && processed == expected_processed &&
            state.before_calls == 1 &&
            state.method_calls == (abort_phase != 1) &&
            state.after_calls == (abort_phase != 1),
        "batch callback result or execution count changed");
  batch_metadata(&state, &messages[0].message, sizeof(messages[0]), 2,
                 abort_phase == 1 ? 1 : 3, 0);
  for (index = 0; index < 2; ++index)
    check(messages[index].padding[0] == 0x13579bdfUL &&
              messages[index].padding[1] == 0x2468ace0UL,
          "message stride overwrote padding");
  check(cpkt_openssl_BIO_close(bio) == 1 &&
            cpkt_openssl_BIO_meth_close(batch_method) == 1,
        "batch BIO cleanup failed");
  for (index = 0; index < 4; ++index)
    BIO_ADDR_free(state.addresses[index]);
}

int main(void) {
  int explicit_context;
  int extended;
  int receive;
  int abort_phase;
  check(CRYPTO_set_mem_functions(test_malloc, test_realloc, test_free) == 1,
        "could not install allocator hooks");
  library_context = OSSL_LIB_CTX_new();
  check(library_context != NULL, "library context allocation failed");
  for (explicit_context = 0; explicit_context < 2; ++explicit_context) {
    run_case(CREATE_SUCCESS, explicit_context);
    run_case(CREATE_REJECT, explicit_context);
    run_case(CREATE_EX_DATA_FAILURE, explicit_context);
  }
  for (extended = 0; extended < 2; ++extended)
    for (receive = 0; receive < 2; ++receive)
      for (abort_phase = 0; abort_phase < 4; ++abort_phase)
        run_batch_case(extended, receive, abort_phase);
  OSSL_LIB_CTX_free(library_context);
  return EXIT_SUCCESS;
}
