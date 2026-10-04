/* Private C99 boundary. Public C89 values never impersonate native ABI types.
 */
#include <cmocka.h>
#include <cpkt/cmocka_types.h>
#include <stdlib.h>
#include <string.h>

static __thread const char *cpkt_cmocka_file;
static __thread int cpkt_cmocka_line;

static uintmax_t cpkt_words_native(CpktCmockaWords value) {
  return ((uintmax_t)(value.high & 0xffffffffUL) << 32) |
         (uintmax_t)(value.low & 0xffffffffUL);
}
/** Construct an exact 64-bit two's-complement bit pattern from two C89 words.
 */
CpktCmockaWords cpkt_cmocka_words(unsigned long high, unsigned long low) {
  CpktCmockaWords value;
  value.high = high & 0xffffffffUL;
  value.low = low & 0xffffffffUL;
  return value;
}
static CpktCMockaValueData cpkt_value_public(CMockaValueData value) {
  CpktCMockaValueData result;
  memset(&result, 0, sizeof(result));
  result.kind = 5;
  result.int_val = (long)value.int_val;
  result.uint_val = (unsigned long)value.uint_val;
  result.float_val = value.float_val;
  result.real_val = value.real_val;
  result.ptr = value.ptr;
  result.const_ptr = value.const_ptr;
  result.func = value.func;
  result.words = cpkt_cmocka_words((unsigned long)(value.uint_val >> 32),
                                   (unsigned long)value.uint_val);
  return result;
}
static CMockaValueData cpkt_value_native(CpktCMockaValueData value) {
  CMockaValueData result = {0};
  switch (value.kind) {
  case 0:
    result.int_val = value.int_val;
    break;
  case 1:
    result.uint_val = value.uint_val;
    break;
  case 2:
    result.const_ptr = value.const_ptr;
    break;
  case 3:
    result.float_val = value.float_val;
    break;
  case 4:
    result.real_val = value.real_val;
    break;
  case 5:
    result.uint_val = cpkt_words_native(value.words);
    break;
  default:
    abort();
  }
  return result;
}
/** Construct a signed C89 scalar. Use value_words for full 64-bit values. */
CpktCMockaValueData cpkt_cmocka_value_int(long value) {
  CpktCMockaValueData result;
  memset(&result, 0, sizeof(result));
  result.kind = 0;
  result.int_val = value;
  return result;
}
/** Construct an unsigned C89 scalar. */
CpktCMockaValueData cpkt_cmocka_value_uint(unsigned long value) {
  CpktCMockaValueData result;
  memset(&result, 0, sizeof(result));
  result.kind = 1;
  result.uint_val = value;
  return result;
}
/** Construct a borrowed pointer mock. */
CpktCMockaValueData cpkt_cmocka_value_ptr(const void *value) {
  CpktCMockaValueData result;
  memset(&result, 0, sizeof(result));
  result.kind = 2;
  result.const_ptr = value;
  return result;
}
/** Construct a native single-precision mock. */
CpktCMockaValueData cpkt_cmocka_value_float(float value) {
  CpktCMockaValueData result;
  memset(&result, 0, sizeof(result));
  result.kind = 3;
  result.float_val = value;
  return result;
}
/** Construct a native double-precision mock. */
CpktCMockaValueData cpkt_cmocka_value_double(double value) {
  CpktCMockaValueData result;
  memset(&result, 0, sizeof(result));
  result.kind = 4;
  result.real_val = value;
  return result;
}
/** Construct a full-width mock without C99 integer types. */
CpktCMockaValueData cpkt_cmocka_value_words(CpktCmockaWords value) {
  CpktCMockaValueData result;
  memset(&result, 0, sizeof(result));
  result.kind = 5;
  result.words = value;
  return result;
}
/** Check the explicitly named native return queue. */
int cpkt_cmocka_has_mock(const char *function) {
  return _has_mock(function) ? 1 : 0;
}
/** Empty legacy unit callback. */
void cpkt_cmocka_unit_test_dummy(void **state) { (void)state; }
/** Save caller location for the C89 variadic-function failure entrypoint. */
void cpkt_cmocka_location(const char *file, int line) {
  cpkt_cmocka_file = file;
  cpkt_cmocka_line = line;
}
/** Preserve native printing and longjmp failure semantics at caller location.
 */
void cpkt_cmocka_fail_msg(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vprint_error(format, args);
  va_end(args);
  _fail(cpkt_cmocka_file, cpkt_cmocka_line);
}

/* Native set assertions may longjmp on failure. Keep converted heap arrays
 * registered until their native call returns or the enclosing group ends. */
struct CpktSetBuffer {
  struct CpktSetBuffer *next;
  void *data;
};
static __thread struct CpktSetBuffer *cpkt_owned_set_buffers;

static void *cpkt_set_buffer_alloc(size_t count, size_t element_size,
                                   const char *file, int line) {
  struct CpktSetBuffer *buffer;
  if (count > (size_t)-1 / element_size)
    _fail(file, line);
  buffer = malloc(sizeof(*buffer));
  if (!buffer)
    _fail(file, line);
  buffer->data = malloc((count ? count : 1) * element_size);
  if (!buffer->data) {
    free(buffer);
    _fail(file, line);
  }
  buffer->next = cpkt_owned_set_buffers;
  cpkt_owned_set_buffers = buffer;
  return buffer->data;
}

static void cpkt_set_buffer_free(void *data) {
  struct CpktSetBuffer **cursor = &cpkt_owned_set_buffers;
  struct CpktSetBuffer *buffer;
  while (*cursor && (*cursor)->data != data)
    cursor = &(*cursor)->next;
  if (!*cursor)
    abort();
  buffer = *cursor;
  *cursor = buffer->next;
  free(buffer->data);
  free(buffer);
}

/* Native owns its own queue event. The C89 event and conversion context are
 * facade-owned, including when a callback fails or reenters a second check. */
struct CpktCheckContext {
  struct CpktCheckContext *previous;
  struct CpktCheckContext *next;
  CpktCheckParameterValueData callback;
  CpktCheckParameterValue old_callback;
  CpktCMockaValueData data;
  void *event;
  int remaining;
  unsigned int active;
};
static __thread struct CpktCheckContext *cpkt_owned_check_contexts;
static void cpkt_release_check_context(struct CpktCheckContext *context) {
  if (context->previous)
    context->previous->next = context->next;
  else
    cpkt_owned_check_contexts = context->next;
  if (context->next)
    context->next->previous = context->previous;
  free(context->event);
  free(context);
}
static void cpkt_retain_check_context(struct CpktCheckContext *context,
                                      void *event, int count) {
  context->event = event;
  context->remaining = count;
  context->active = 0;
  context->previous = NULL;
  context->next = cpkt_owned_check_contexts;
  if (context->next)
    context->next->previous = context;
  cpkt_owned_check_contexts = context;
}
static int cpkt_check_callback(CMockaValueData value, CMockaValueData data) {
  struct CpktCheckContext *context = data.ptr;
  int result;
  ++context->active;
  if (context->callback)
    result = context->callback(cpkt_value_public(value), context->data);
  else
    result = context->old_callback((unsigned long)value.uint_val,
                                   context->data.uint_val);
  if (context->remaining > 0)
    --context->remaining;
  --context->active;
  if (context->remaining == 0 && context->active == 0)
    cpkt_release_check_context(context);
  return result;
}
/** Queue a typed C89 callback through the native event/diagnostic machinery. */
void cpkt_cmocka_expect_check_data(const char *function, const char *parameter,
                                   const char *file, int line,
                                   CpktCheckParameterValueData callback,
                                   CpktCMockaValueData data,
                                   CpktCheckParameterEventData *event,
                                   int count) {
  struct CpktCheckContext *context = malloc(sizeof(*context));
  CMockaValueData native = {0};
  if (!context) {
    free(event);
    _fail(file, line);
  }
  context->callback = callback;
  context->old_callback = NULL;
  context->data = data;
  cpkt_retain_check_context(context, event, count);
  if (event) {
    event->location.file = file;
    event->location.line = line;
    event->parameter_name = parameter;
    event->check_value = callback;
    event->check_value_data = data;
  }
  native.ptr = context;
  _expect_check_data(function, parameter, file, line, cpkt_check_callback,
                     native, NULL, count);
}
/** Queue a legacy scalar callback without an incompatible function-pointer
 * cast. */
void cpkt_cmocka_expect_check(const char *function, const char *parameter,
                              const char *file, int line,
                              CpktCheckParameterValue callback,
                              unsigned long data,
                              CpktCheckParameterEvent *event, int count) {
  struct CpktCheckContext *context = malloc(sizeof(*context));
  CMockaValueData native = {0};
  if (!context) {
    free(event);
    _fail(file, line);
  }
  context->callback = NULL;
  context->old_callback = callback;
  context->data = cpkt_cmocka_value_uint(data);
  cpkt_retain_check_context(context, event, count);
  if (event) {
    event->location.file = file;
    event->location.line = line;
    event->parameter_name = parameter;
    event->check_value = callback;
    event->check_value_data = data;
  }
  native.ptr = context;
  _expect_check_data(function, parameter, file, line, cpkt_check_callback,
                     native, NULL, count);
}
/** Run native cmocka tests, converting descriptors and releasing transferred
 * C89 check events after native success, failure and optional queue cleanup. */
int cpkt_cmocka_run_group_tests(const char *group_name,
                                const struct CpktCMUnitTest *tests,
                                size_t num_tests,
                                CpktCMFixtureFunction group_setup,
                                CpktCMFixtureFunction group_teardown) {
  struct CMUnitTest *native;
  struct CpktCheckContext *previous = cpkt_owned_check_contexts;
  struct CpktSetBuffer *previous_sets = cpkt_owned_set_buffers;
  size_t index;
  int result;
  if (num_tests > (size_t)-1 / sizeof(*native))
    _fail(__FILE__, __LINE__);
  native = malloc((num_tests ? num_tests : 1) * sizeof(*native));
  if (!native)
    _fail(__FILE__, __LINE__);
  for (index = 0; index < num_tests; ++index) {
    native[index].name = tests[index].name;
    native[index].test_func = tests[index].test_func;
    native[index].setup_func = tests[index].setup_func;
    native[index].teardown_func = tests[index].teardown_func;
    native[index].initial_state = tests[index].initial_state;
  }
  cpkt_owned_check_contexts = NULL;
  cpkt_owned_set_buffers = NULL;
  result = _cmocka_run_group_tests(group_name, native, num_tests, group_setup,
                                   group_teardown);
  while (cpkt_owned_set_buffers)
    cpkt_set_buffer_free(cpkt_owned_set_buffers->data);
  cpkt_owned_set_buffers = previous_sets;
  while (cpkt_owned_check_contexts)
    cpkt_release_check_context(cpkt_owned_check_contexts);
  cpkt_owned_check_contexts = previous;
  free(native);
  return result;
}
/** Configure native diagnostic output through the fixed-width private boundary.
 */
void cpkt_cmocka_set_message_output(unsigned long output) {
  cmocka_set_message_output((uint32_t)output);
}
#include <cpkt/cmocka_bridge.inc>
