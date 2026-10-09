#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cpkt/lua_runtime.h>

#define assert_true(value) cpkt_mock_assert_true((value) != 0, #value, __LINE__)
#define assert_null(value)                                                     \
  cpkt_mock_assert_true((value) == NULL, #value, __LINE__)
#define assert_non_null(value)                                                 \
  cpkt_mock_assert_true((value) != NULL, #value, __LINE__)
#define assert_ptr_equal(actual, expected)                                     \
  cpkt_mock_assert_ptr_equal((const void *)(actual), (const void *)(expected), \
                             __LINE__)
#define assert_int_equal(actual, expected)                                     \
  cpkt_mock_assert_int_equal((long)(actual), (long)(expected), __LINE__)
#define assert_string_equal(actual, expected)                                  \
  cpkt_mock_assert_string_equal((actual), (expected), __LINE__)

static void cpkt_mock_fail(const char *message, int line) {
  fprintf(stderr, "lua_runtime_mock_test:%d: %s\n", line, message);
  exit(1);
}

static void cpkt_mock_assert_true(int ok, const char *expression, int line) {
  if (!ok) {
    cpkt_mock_fail(expression, line);
  }
}

static void cpkt_mock_assert_int_equal(long actual, long expected, int line) {
  char message[160];

  if (actual != expected) {
    snprintf(message, sizeof(message), "expected %ld, got %ld", expected,
             actual);
    cpkt_mock_fail(message, line);
  }
}

static void cpkt_mock_assert_ptr_equal(const void *actual, const void *expected,
                                       int line) {
  char message[160];

  if (actual != expected) {
    snprintf(message, sizeof(message), "expected pointer %p, got %p", expected,
             actual);
    cpkt_mock_fail(message, line);
  }
}

static void cpkt_mock_assert_string_equal(const char *actual,
                                          const char *expected, int line) {
  char message[256];

  if (actual == NULL || expected == NULL || strcmp(actual, expected) != 0) {
    snprintf(message, sizeof(message), "expected string '%s', got '%s'",
             expected != NULL ? expected : "(null)",
             actual != NULL ? actual : "(null)");
    cpkt_mock_fail(message, line);
  }
}

struct mock_allocator {
  size_t alloc_calls;
  size_t free_calls;
  size_t bytes_live;
  size_t fail_after_calls;
};

static void *mock_alloc(void *user, size_t size) {
  struct mock_allocator *allocator;
  void *ptr;

  allocator = (struct mock_allocator *)user;
  allocator->alloc_calls += 1;
  if (allocator->fail_after_calls != 0 &&
      allocator->alloc_calls > allocator->fail_after_calls) {
    return NULL;
  }
  ptr = malloc(size);
  if (ptr != NULL) {
    allocator->bytes_live += size;
  }
  return ptr;
}

static void mock_free(void *user, void *ptr, size_t size) {
  struct mock_allocator *allocator;

  allocator = (struct mock_allocator *)user;
  allocator->free_calls += 1;
  if (allocator->bytes_live >= size) {
    allocator->bytes_live -= size;
  } else {
    allocator->bytes_live = 0;
  }
  free(ptr);
}

static cpkt_lua_runtime_status mock_new(cpkt_lua_runtime **runtime,
                                        struct mock_allocator *allocator,
                                        size_t max_bytes) {
  cpkt_lua_runtime_allocator_config config;

  memset(&config, 0, sizeof(config));
  config.user = allocator;
  config.alloc_fn = mock_alloc;
  config.free_fn = mock_free;
  config.max_bytes = max_bytes;
  return cpkt_lua_runtime_new_with_allocator(runtime, &config);
}

static int mock_module_open(void *lua_state) {
  return lua_state != NULL ? 1 : 0;
}

static void test_mock_sink_exercises_facade_surface(void) {
  struct mock_allocator allocator;
  cpkt_lua_runtime *runtime;
  const char *argv[2];
  static const unsigned char module_source[] = "return {value = 'mock'}";
  static const unsigned char run_source[] = "return true";

  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;

  assert_string_equal(cpkt_lua_runtime_lua_version(), "Lua 5.5.0-mock");
  assert_int_equal(mock_new(&runtime, &allocator, 1024 * 1024),
                   CPKT_LUA_RUNTIME_OK);
  assert_non_null(runtime);

  cpkt_lua_runtime_set_context(runtime, (void *)"mock-context");
  assert_ptr_equal(cpkt_lua_runtime_context(runtime), (void *)"mock-context");
  assert_int_equal(
      cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_ALL),
      CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_traceback(runtime, 1),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_package_path(runtime, "a/?.lua"),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_prepend_package_path(runtime, "b/?.lua"),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_package_cpath(runtime, "a/?.so"),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_prepend_package_cpath(runtime, "b/?.so"),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_global_string(runtime, "name", "value"),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_global_boolean(runtime, "enabled", 1),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_global_integer(runtime, "count", 7),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_global_number(runtime, "ratio", 2.5),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_register_c_module(runtime, "mock_host",
                                                      mock_module_open),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_register_lua_module(
                       runtime, "mock_chunk", module_source,
                       sizeof(module_source) - 1, "mock_chunk.lua"),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_require(runtime, "mock_chunk"),
                   CPKT_LUA_RUNTIME_OK);

  argv[0] = "one";
  argv[1] = "two";
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, run_source,
                                               sizeof(run_source) - 1,
                                               "run.lua", 2, argv, 0),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_run_file(runtime, "ok.lua", 2, argv, 0),
                   CPKT_LUA_RUNTIME_OK);
  assert_string_equal(cpkt_lua_runtime_error(runtime), "");

  cpkt_lua_runtime_free(runtime);
  assert_true(allocator.alloc_calls > 0);
  assert_true(allocator.free_calls > 0);
  assert_int_equal(allocator.bytes_live, 0);
}

static void test_mock_sink_package_errors_without_package_library(void) {
  struct mock_allocator allocator;
  cpkt_lua_runtime *runtime;

  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;

  assert_int_equal(mock_new(&runtime, &allocator, 1024 * 1024),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_package_path(runtime, "?.lua"),
                   CPKT_LUA_RUNTIME_ERR_RUNTIME);
  assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "package table"));
  cpkt_lua_runtime_free(runtime);
  assert_int_equal(allocator.bytes_live, 0);
}

static void test_mock_sink_load_runtime_and_limit_errors(void) {
  struct mock_allocator allocator;
  cpkt_lua_runtime *runtime;
  static const unsigned char load_error_source[] = "load-error";
  static const unsigned char runtime_error_source[] = "runtime-error";
  static const unsigned char limit_source[] = "while true";
  static const unsigned char hook_bypass_source[] =
      "pcall(function() debug.sethook() end)\n"
      "while true";

  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;

  assert_int_equal(mock_new(&runtime, &allocator, 1024 * 1024),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_traceback(runtime, 1),
                   CPKT_LUA_RUNTIME_OK);

  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, load_error_source,
                                               sizeof(load_error_source) - 1,
                                               "bad.lua", 0, NULL, 0),
                   CPKT_LUA_RUNTIME_ERR_LOAD);
  assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "bad.lua"));

  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, runtime_error_source,
                                               sizeof(runtime_error_source) - 1,
                                               "runtime.lua", 0, NULL, 0),
                   CPKT_LUA_RUNTIME_ERR_RUNTIME);
  assert_non_null(
      strstr(cpkt_lua_runtime_error(runtime), "mock runtime error"));

  assert_int_equal(cpkt_lua_runtime_set_instruction_limit(runtime, 1000),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, limit_source,
                                               sizeof(limit_source) - 1,
                                               "limit.lua", 0, NULL, 0),
                   CPKT_LUA_RUNTIME_ERR_LIMIT);
  assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "instruction limit"));
  assert_int_equal(cpkt_lua_runtime_clear_instruction_limit(runtime),
                   CPKT_LUA_RUNTIME_OK);

  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, hook_bypass_source,
                                               sizeof(hook_bypass_source) - 1,
                                               "debug-hook-bypass.lua", 0, NULL,
                                               CPKT_LUA_RUNTIME_OPEN_LIBS),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_instruction_limit(runtime, 1000),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, hook_bypass_source,
                                               sizeof(hook_bypass_source) - 1,
                                               "debug-hook-bypass.lua", 0, NULL,
                                               CPKT_LUA_RUNTIME_OPEN_LIBS),
                   CPKT_LUA_RUNTIME_ERR_LIMIT);
  assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "instruction limit"));
  assert_int_equal(cpkt_lua_runtime_clear_instruction_limit(runtime),
                   CPKT_LUA_RUNTIME_OK);

  cpkt_lua_runtime_free(runtime);
  assert_int_equal(allocator.bytes_live, 0);
}

static void test_mock_sink_allocator_failures_are_reported(void) {
  struct mock_allocator allocator;
  cpkt_lua_runtime *runtime;
  static const unsigned char module_source[] = "return true";
  static const unsigned char run_source[] = "return true";

  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;

  assert_int_equal(mock_new(&runtime, &allocator, 64),
                   CPKT_LUA_RUNTIME_ERR_ALLOC);
  assert_null(runtime);
  assert_int_equal(allocator.bytes_live, 0);

  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;
  assert_int_equal(mock_new(&runtime, &allocator, 1024 * 1024),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(
      cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_BASE |
                                              CPKT_LUA_RUNTIME_LIB_PACKAGE),
      CPKT_LUA_RUNTIME_OK);

  allocator.fail_after_calls = allocator.alloc_calls;
  assert_int_equal(cpkt_lua_runtime_register_c_module(
                       runtime, "mock_oom_c_module", mock_module_open),
                   CPKT_LUA_RUNTIME_ERR_ALLOC);

  allocator.fail_after_calls = 0;
  assert_int_equal(cpkt_lua_runtime_register_c_module(
                       runtime, "mock_ok_c_module", mock_module_open),
                   CPKT_LUA_RUNTIME_OK);

  allocator.fail_after_calls = allocator.alloc_calls + 1;
  assert_int_equal(cpkt_lua_runtime_register_lua_module(
                       runtime, "large", module_source,
                       sizeof(module_source) - 1, "large.lua"),
                   CPKT_LUA_RUNTIME_ERR_ALLOC);

  allocator.fail_after_calls = allocator.alloc_calls;
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, run_source,
                                               sizeof(run_source) - 1,
                                               "mock-load-oom.lua", 0, NULL, 0),
                   CPKT_LUA_RUNTIME_ERR_ALLOC);

  cpkt_lua_runtime_free(runtime);
  assert_int_equal(allocator.bytes_live, 0);
}

int main(void) {
  test_mock_sink_exercises_facade_surface();
  test_mock_sink_package_errors_without_package_library();
  test_mock_sink_load_runtime_and_limit_errors();
  test_mock_sink_allocator_failures_are_reported();
  return 0;
}
