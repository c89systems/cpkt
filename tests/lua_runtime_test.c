#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include <cpkt/lua_runtime.h>

struct test_allocator {
  size_t alloc_calls;
  size_t realloc_calls;
  size_t free_calls;
  size_t bytes_live;
  size_t fail_after_calls;
};

static void *test_alloc(void *user, size_t size) {
  struct test_allocator *allocator;
  void *ptr;

  allocator = (struct test_allocator *)user;
  allocator->alloc_calls += 1;
  if (allocator->fail_after_calls != 0 &&
      allocator->alloc_calls + allocator->realloc_calls >
          allocator->fail_after_calls) {
    return NULL;
  }
  ptr = malloc(size);
  if (ptr != NULL) {
    allocator->bytes_live += size;
  }
  return ptr;
}

static void *facade_test_realloc(void *user, void *ptr, size_t old_size,
                                 size_t new_size) {
  struct test_allocator *allocator;
  void *next;

  allocator = (struct test_allocator *)user;
  allocator->realloc_calls += 1;
  if (allocator->fail_after_calls != 0 &&
      allocator->alloc_calls + allocator->realloc_calls >
          allocator->fail_after_calls) {
    return NULL;
  }
  next = realloc(ptr, new_size);
  if (next != NULL) {
    if (allocator->bytes_live >= old_size) {
      allocator->bytes_live -= old_size;
    } else {
      allocator->bytes_live = 0;
    }
    allocator->bytes_live += new_size;
  }
  return next;
}

static void facade_test_free(void *user, void *ptr, size_t size) {
  struct test_allocator *allocator;

  allocator = (struct test_allocator *)user;
  allocator->free_calls += 1;
  if (allocator->bytes_live >= size) {
    allocator->bytes_live -= size;
  } else {
    allocator->bytes_live = 0;
  }
  free(ptr);
}

static int test_module_open(void *lua_state) {
  return lua_state != NULL ? 0 : 0;
}

static size_t
test_allocator_call_count(const struct test_allocator *allocator) {
  return allocator->alloc_calls + allocator->realloc_calls;
}

static cpkt_lua_runtime_status
test_new_with_allocator(cpkt_lua_runtime **runtime,
                        struct test_allocator *allocator) {
  cpkt_lua_runtime_allocator_config config;

  memset(&config, 0, sizeof(config));
  config.user = allocator;
  config.alloc_fn = test_alloc;
  config.realloc_fn = facade_test_realloc;
  config.free_fn = facade_test_free;
  config.max_bytes = 1024 * 1024;
  return cpkt_lua_runtime_new_with_allocator(runtime, &config);
}

static void test_invalid_allocator_config(void **state) {
  cpkt_lua_runtime_allocator_config config;
  cpkt_lua_runtime *runtime;

  (void)state;
  memset(&config, 0, sizeof(config));
  runtime = NULL;
  config.alloc_fn = test_alloc;
  assert_int_equal(cpkt_lua_runtime_new_with_allocator(&runtime, &config),
                   CPKT_LUA_RUNTIME_ERR_ARG);
  assert_null(runtime);

  memset(&config, 0, sizeof(config));
  runtime = NULL;
  config.free_fn = facade_test_free;
  assert_int_equal(cpkt_lua_runtime_new_with_allocator(&runtime, &config),
                   CPKT_LUA_RUNTIME_ERR_ARG);
  assert_null(runtime);

  memset(&config, 0, sizeof(config));
  runtime = NULL;
  config.realloc_fn = facade_test_realloc;
  assert_int_equal(cpkt_lua_runtime_new_with_allocator(&runtime, &config),
                   CPKT_LUA_RUNTIME_ERR_ARG);
  assert_null(runtime);
}

static void test_custom_allocator_balances_on_free(void **state) {
  struct test_allocator allocator;
  cpkt_lua_runtime *runtime;
  static const unsigned char chunk[] = "return true";

  (void)state;
  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;
  assert_int_equal(test_new_with_allocator(&runtime, &allocator),
                   CPKT_LUA_RUNTIME_OK);
  assert_non_null(runtime);
  assert_int_equal(
      cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_BASE |
                                              CPKT_LUA_RUNTIME_LIB_PACKAGE),
      CPKT_LUA_RUNTIME_OK);
  assert_int_equal(
      cpkt_lua_runtime_register_lua_module(runtime, "balanced", chunk,
                                           sizeof(chunk) - 1, "balanced.lua"),
      CPKT_LUA_RUNTIME_OK);
  assert_true(allocator.alloc_calls > 0);
  cpkt_lua_runtime_free(runtime);
  assert_int_equal(allocator.bytes_live, 0);
  assert_true(allocator.free_calls > 0);
}

static void test_open_libs_reports_allocator_failure(void **state) {
  struct test_allocator allocator;
  cpkt_lua_runtime *runtime;
  cpkt_lua_runtime_status status;

  (void)state;
  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;
  assert_int_equal(test_new_with_allocator(&runtime, &allocator),
                   CPKT_LUA_RUNTIME_OK);

  allocator.fail_after_calls = test_allocator_call_count(&allocator) + 4;
  status = cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_ALL);
  assert_int_equal(status, CPKT_LUA_RUNTIME_ERR_ALLOC);

  cpkt_lua_runtime_free(runtime);
  assert_int_equal(allocator.bytes_live, 0);
}

static void test_run_with_open_libs_reports_allocator_failure(void **state) {
  struct test_allocator allocator;
  cpkt_lua_runtime *runtime;
  cpkt_lua_runtime_status status;
  static const unsigned char source[] = "return true";

  (void)state;
  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;
  assert_int_equal(test_new_with_allocator(&runtime, &allocator),
                   CPKT_LUA_RUNTIME_OK);

  allocator.fail_after_calls = test_allocator_call_count(&allocator) + 4;
  status = cpkt_lua_runtime_run_buffer(runtime, source, sizeof(source) - 1,
                                       "allocator-openlibs.lua", 0, NULL,
                                       CPKT_LUA_RUNTIME_OPEN_LIBS);
  assert_int_equal(status, CPKT_LUA_RUNTIME_ERR_ALLOC);

  cpkt_lua_runtime_free(runtime);
  assert_int_equal(allocator.bytes_live, 0);
}

static void
test_allocator_failure_state_does_not_misclassify_later_runtime_error(
    void **state) {
  struct test_allocator allocator;
  cpkt_lua_runtime *runtime;
  static const unsigned char source[] = "error('normal runtime failure')";

  (void)state;
  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;
  assert_int_equal(test_new_with_allocator(&runtime, &allocator),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(
      cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_BASE),
      CPKT_LUA_RUNTIME_OK);

  allocator.fail_after_calls = test_allocator_call_count(&allocator);
  assert_int_equal(
      cpkt_lua_runtime_set_global_string(runtime, "oom_probe", "value"),
      CPKT_LUA_RUNTIME_ERR_ALLOC);

  allocator.fail_after_calls = 0;
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, source,
                                               sizeof(source) - 1,
                                               "later-runtime.lua", 0, NULL, 0),
                   CPKT_LUA_RUNTIME_ERR_RUNTIME);
  assert_non_null(
      strstr(cpkt_lua_runtime_error(runtime), "normal runtime failure"));

  cpkt_lua_runtime_free(runtime);
  assert_int_equal(allocator.bytes_live, 0);
}

static void
test_public_lua_allocating_operations_report_allocator_failure(void **state) {
  struct test_allocator allocator;
  cpkt_lua_runtime *runtime;

  (void)state;
  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;
  assert_int_equal(test_new_with_allocator(&runtime, &allocator),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(
      cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_BASE |
                                              CPKT_LUA_RUNTIME_LIB_PACKAGE),
      CPKT_LUA_RUNTIME_OK);

  allocator.fail_after_calls = test_allocator_call_count(&allocator);
  assert_int_equal(
      cpkt_lua_runtime_register_c_module(runtime, "oom_c_module", NULL),
      CPKT_LUA_RUNTIME_ERR_ARG);
  assert_int_equal(cpkt_lua_runtime_register_c_module(runtime, "oom_c_module",
                                                      test_module_open),
                   CPKT_LUA_RUNTIME_ERR_ALLOC);

  allocator.fail_after_calls = 0;
  assert_int_equal(cpkt_lua_runtime_register_c_module(runtime, "ok_c_module",
                                                      test_module_open),
                   CPKT_LUA_RUNTIME_OK);

  allocator.fail_after_calls = test_allocator_call_count(&allocator);
  assert_int_equal(cpkt_lua_runtime_set_package_path(runtime, "?.lua"),
                   CPKT_LUA_RUNTIME_ERR_ALLOC);

  allocator.fail_after_calls = 0;
  assert_int_equal(cpkt_lua_runtime_set_package_path(runtime, "?.lua"),
                   CPKT_LUA_RUNTIME_OK);

  allocator.fail_after_calls = test_allocator_call_count(&allocator);
  assert_int_equal(
      cpkt_lua_runtime_set_global_string(runtime, "oom_global", "value"),
      CPKT_LUA_RUNTIME_ERR_ALLOC);

  allocator.fail_after_calls = 0;
  assert_int_equal(
      cpkt_lua_runtime_set_global_string(runtime, "ok_global", "value"),
      CPKT_LUA_RUNTIME_OK);

  allocator.fail_after_calls = test_allocator_call_count(&allocator);
  assert_int_equal(cpkt_lua_runtime_require(runtime, "missing_after_oom"),
                   CPKT_LUA_RUNTIME_ERR_ALLOC);

  cpkt_lua_runtime_free(runtime);
  assert_int_equal(allocator.bytes_live, 0);
}

static void
test_lua_module_registration_allocator_failure_releases_chunk(void **state) {
  struct test_allocator allocator;
  cpkt_lua_runtime *runtime;
  static const unsigned char source[] = "return true";

  (void)state;
  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;
  assert_int_equal(test_new_with_allocator(&runtime, &allocator),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(
      cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_BASE |
                                              CPKT_LUA_RUNTIME_LIB_PACKAGE),
      CPKT_LUA_RUNTIME_OK);

  allocator.fail_after_calls = test_allocator_call_count(&allocator) + 4;
  assert_int_equal(cpkt_lua_runtime_register_lua_module(
                       runtime, "oom_lua_module", source, sizeof(source) - 1,
                       "oom_lua_module.lua"),
                   CPKT_LUA_RUNTIME_ERR_ALLOC);

  allocator.fail_after_calls = 0;
  assert_int_equal(cpkt_lua_runtime_register_lua_module(
                       runtime, "ok_lua_module", source, sizeof(source) - 1,
                       "ok_lua_module.lua"),
                   CPKT_LUA_RUNTIME_OK);

  cpkt_lua_runtime_free(runtime);
  assert_int_equal(allocator.bytes_live, 0);
}

static void
test_load_time_allocator_failure_reports_alloc_for_buffer(void **state) {
  struct test_allocator allocator;
  cpkt_lua_runtime *runtime;
  static const unsigned char source[] = "return 1";

  (void)state;
  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;
  assert_int_equal(test_new_with_allocator(&runtime, &allocator),
                   CPKT_LUA_RUNTIME_OK);

  allocator.fail_after_calls = test_allocator_call_count(&allocator);
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, source,
                                               sizeof(source) - 1,
                                               "load-oom.lua", 0, NULL, 0),
                   CPKT_LUA_RUNTIME_ERR_ALLOC);

  allocator.fail_after_calls = 0;
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, source,
                                               sizeof(source) - 1,
                                               "load-ok.lua", 0, NULL, 0),
                   CPKT_LUA_RUNTIME_OK);

  cpkt_lua_runtime_free(runtime);
  assert_int_equal(allocator.bytes_live, 0);
}

static void
test_load_time_allocator_failure_reports_alloc_for_file(void **state) {
  struct test_allocator allocator;
  cpkt_lua_runtime *runtime;
  FILE *file;
  const char *path;

  (void)state;
  path = "lua-runtime-load-oom-test.lua";
  file = fopen(path, "wb");
  assert_non_null(file);
  assert_true(fputs("return 1\n", file) >= 0);
  assert_int_equal(fclose(file), 0);

  memset(&allocator, 0, sizeof(allocator));
  runtime = NULL;
  assert_int_equal(test_new_with_allocator(&runtime, &allocator),
                   CPKT_LUA_RUNTIME_OK);

  allocator.fail_after_calls = test_allocator_call_count(&allocator);
  assert_int_equal(cpkt_lua_runtime_run_file(runtime, path, 0, NULL, 0),
                   CPKT_LUA_RUNTIME_ERR_ALLOC);

  allocator.fail_after_calls = 0;
  assert_int_equal(cpkt_lua_runtime_run_file(runtime, path, 0, NULL, 0),
                   CPKT_LUA_RUNTIME_OK);

  cpkt_lua_runtime_free(runtime);
  assert_int_equal(allocator.bytes_live, 0);
  assert_int_equal(remove(path), 0);
}

static void test_memory_limit_fails_construction(void **state) {
  cpkt_lua_runtime *runtime;

  (void)state;
  runtime = NULL;
  assert_int_equal(cpkt_lua_runtime_new_with_limit(&runtime, 64),
                   CPKT_LUA_RUNTIME_ERR_ALLOC);
  assert_null(runtime);
}

static void test_runtime_error_reports_traceback(void **state) {
  cpkt_lua_runtime *runtime;
  static const unsigned char source[] = "error('facade failure')";
  cpkt_lua_runtime_status status;

  (void)state;
  runtime = NULL;
  assert_int_equal(cpkt_lua_runtime_new(&runtime), CPKT_LUA_RUNTIME_OK);
  assert_int_equal(
      cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_BASE),
      CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_traceback(runtime, 1),
                   CPKT_LUA_RUNTIME_OK);
  status = cpkt_lua_runtime_run_buffer(runtime, source, sizeof(source) - 1,
                                       "runtime.lua", 0, NULL, 0);
  assert_int_equal(status, CPKT_LUA_RUNTIME_ERR_RUNTIME);
  assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "facade failure"));
  assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "stack traceback"));
  cpkt_lua_runtime_free(runtime);
}

static void test_load_error_is_recoverable(void **state) {
  cpkt_lua_runtime *runtime;
  static const unsigned char bad_source[] = "function";
  static const unsigned char good_source[] = "return true";

  (void)state;
  runtime = NULL;
  assert_int_equal(cpkt_lua_runtime_new(&runtime), CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, bad_source,
                                               sizeof(bad_source) - 1,
                                               "bad.lua", 0, NULL, 0),
                   CPKT_LUA_RUNTIME_ERR_LOAD);
  assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "bad.lua"));
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, good_source,
                                               sizeof(good_source) - 1,
                                               "good.lua", 0, NULL, 0),
                   CPKT_LUA_RUNTIME_OK);
  assert_string_equal(cpkt_lua_runtime_error(runtime), "");
  cpkt_lua_runtime_free(runtime);
}

static void test_instruction_limit(void **state) {
  cpkt_lua_runtime *runtime;
  static const unsigned char source[] = "while true do end";

  (void)state;
  runtime = NULL;
  assert_int_equal(cpkt_lua_runtime_new(&runtime), CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_instruction_limit(runtime, 1000),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, source,
                                               sizeof(source) - 1, "limit.lua",
                                               0, NULL, 0),
                   CPKT_LUA_RUNTIME_ERR_LIMIT);
  assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "instruction limit"));
  assert_int_equal(cpkt_lua_runtime_clear_instruction_limit(runtime),
                   CPKT_LUA_RUNTIME_OK);
  cpkt_lua_runtime_free(runtime);
}

static void
test_instruction_limit_survives_debug_sethook_attempt(void **state) {
  cpkt_lua_runtime *runtime;
  static const unsigned char source[] =
      "pcall(function() debug.sethook() end)\n"
      "while true do end";

  (void)state;
  runtime = NULL;
  assert_int_equal(cpkt_lua_runtime_new(&runtime), CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_instruction_limit(runtime, 1000),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, source,
                                               sizeof(source) - 1,
                                               "debug-hook-bypass.lua", 0, NULL,
                                               CPKT_LUA_RUNTIME_OPEN_LIBS),
                   CPKT_LUA_RUNTIME_ERR_LIMIT);
  assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "instruction limit"));
  cpkt_lua_runtime_free(runtime);
}

static void test_instruction_limit_applies_to_coroutine_resume(void **state) {
  cpkt_lua_runtime *runtime;
  static const unsigned char source[] =
      "local co = coroutine.create(function()\n"
      "  while true do end\n"
      "end)\n"
      "local ok, err = coroutine.resume(co)\n"
      "if ok then error('coroutine unexpectedly completed') end\n"
      "error(err)";

  (void)state;
  runtime = NULL;
  assert_int_equal(cpkt_lua_runtime_new(&runtime), CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_instruction_limit(runtime, 1000),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(
      cpkt_lua_runtime_run_buffer(runtime, source, sizeof(source) - 1,
                                  "coroutine-resume-limit.lua", 0, NULL,
                                  CPKT_LUA_RUNTIME_OPEN_LIBS),
      CPKT_LUA_RUNTIME_ERR_LIMIT);
  assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "instruction limit"));
  cpkt_lua_runtime_free(runtime);
}

static void test_instruction_limit_applies_to_coroutine_wrap(void **state) {
  cpkt_lua_runtime *runtime;
  static const unsigned char source[] =
      "local run = coroutine.wrap(function()\n"
      "  while true do end\n"
      "end)\n"
      "run()";

  (void)state;
  runtime = NULL;
  assert_int_equal(cpkt_lua_runtime_new(&runtime), CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_instruction_limit(runtime, 1000),
                   CPKT_LUA_RUNTIME_OK);
  assert_int_equal(
      cpkt_lua_runtime_run_buffer(runtime, source, sizeof(source) - 1,
                                  "coroutine-wrap-limit.lua", 0, NULL,
                                  CPKT_LUA_RUNTIME_OPEN_LIBS),
      CPKT_LUA_RUNTIME_ERR_LIMIT);
  assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "instruction limit"));
  cpkt_lua_runtime_free(runtime);
}

static void test_debug_sethook_is_disabled(void **state) {
  cpkt_lua_runtime *runtime;
  static const unsigned char source[] = "debug.sethook(function() end, '', 1)";

  (void)state;
  runtime = NULL;
  assert_int_equal(cpkt_lua_runtime_new(&runtime), CPKT_LUA_RUNTIME_OK);
  assert_int_equal(
      cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_DEBUG),
      CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, source,
                                               sizeof(source) - 1,
                                               "debug-sethook.lua", 0, NULL, 0),
                   CPKT_LUA_RUNTIME_ERR_RUNTIME);
  assert_non_null(
      strstr(cpkt_lua_runtime_error(runtime), "debug.sethook is unavailable"));
  cpkt_lua_runtime_free(runtime);
}

static void test_package_requires_package_library(void **state) {
  cpkt_lua_runtime *runtime;

  (void)state;
  runtime = NULL;
  assert_int_equal(cpkt_lua_runtime_new(&runtime), CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_set_package_path(runtime, "?.lua"),
                   CPKT_LUA_RUNTIME_ERR_RUNTIME);
  assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "package table"));
  cpkt_lua_runtime_free(runtime);
}

static void test_failed_lua_module_registration_releases_chunk(void **state) {
  struct test_allocator allocator;
  cpkt_lua_runtime_allocator_config config;
  cpkt_lua_runtime *runtime;
  unsigned char source[4096];
  int i;

  (void)state;
  memset(&allocator, 0, sizeof(allocator));
  memset(&config, 0, sizeof(config));
  memset(source, ' ', sizeof(source));
  memcpy(source, "return true", sizeof("return true") - 1);

  config.user = &allocator;
  config.alloc_fn = test_alloc;
  config.realloc_fn = facade_test_realloc;
  config.free_fn = facade_test_free;
  config.max_bytes = 1024 * 1024;

  runtime = NULL;
  assert_int_equal(cpkt_lua_runtime_new_with_allocator(&runtime, &config),
                   CPKT_LUA_RUNTIME_OK);
  for (i = 0; i < 300; ++i) {
    assert_int_equal(cpkt_lua_runtime_register_lua_module(
                         runtime, "missing_package", source, sizeof(source),
                         "missing_package.lua"),
                     CPKT_LUA_RUNTIME_ERR_RUNTIME);
    assert_non_null(strstr(cpkt_lua_runtime_error(runtime), "package table"));
  }

  assert_int_equal(
      cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_BASE |
                                              CPKT_LUA_RUNTIME_LIB_PACKAGE),
      CPKT_LUA_RUNTIME_OK);
  assert_int_equal(cpkt_lua_runtime_register_lua_module(
                       runtime, "missing_package", source,
                       sizeof("return true") - 1, "missing_package.lua"),
                   CPKT_LUA_RUNTIME_OK);

  cpkt_lua_runtime_free(runtime);
  assert_int_equal(allocator.bytes_live, 0);
}

struct warning_capture {
  char text[128];
  size_t used;
  int fragments;
  int continuations;
};

static void on_warning(void *context, const char *message, int to_continue) {
  struct warning_capture *capture;
  size_t length;

  capture = (struct warning_capture *)context;
  length = strlen(message);
  assert_true(capture->used + length < sizeof(capture->text));
  memcpy(capture->text + capture->used, message, length + 1);
  capture->used += length;
  capture->fragments++;
  capture->continuations += to_continue != 0;
}

static void test_warning_callback_lifetime_and_replacement(void **state) {
  static const unsigned char source[] = "warn('alpha', 'beta')";
  struct warning_capture first;
  struct warning_capture second;
  cpkt_lua_runtime *runtime;

  (void)state;
  memset(&first, 0, sizeof(first));
  memset(&second, 0, sizeof(second));
  runtime = NULL;
  assert_int_equal(cpkt_lua_runtime_new(&runtime), CPKT_LUA_RUNTIME_OK);
  assert_int_equal(
      cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_BASE),
      CPKT_LUA_RUNTIME_OK);
  cpkt_lua_runtime_set_warning_callback(runtime, on_warning, &first);
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, source,
                                               sizeof(source) - 1, "warn.lua",
                                               0, NULL, 0),
                   CPKT_LUA_RUNTIME_OK);
  assert_string_equal(first.text, "alphabeta");
  assert_true(first.fragments >= 2);
  assert_true(first.continuations >= 1);

  cpkt_lua_runtime_set_warning_callback(runtime, on_warning, &second);
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, source,
                                               sizeof(source) - 1, "warn.lua",
                                               0, NULL, 0),
                   CPKT_LUA_RUNTIME_OK);
  assert_string_equal(second.text, "alphabeta");
  assert_string_equal(first.text, "alphabeta");
  cpkt_lua_runtime_set_warning_callback(runtime, NULL, NULL);
  assert_int_equal(cpkt_lua_runtime_run_buffer(runtime, source,
                                               sizeof(source) - 1, "warn.lua",
                                               0, NULL, 0),
                   CPKT_LUA_RUNTIME_OK);
  assert_string_equal(second.text, "alphabeta");
  cpkt_lua_runtime_free(runtime);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_invalid_allocator_config),
      cmocka_unit_test(test_custom_allocator_balances_on_free),
      cmocka_unit_test(test_open_libs_reports_allocator_failure),
      cmocka_unit_test(test_run_with_open_libs_reports_allocator_failure),
      cmocka_unit_test(
          test_allocator_failure_state_does_not_misclassify_later_runtime_error),
      cmocka_unit_test(
          test_public_lua_allocating_operations_report_allocator_failure),
      cmocka_unit_test(
          test_lua_module_registration_allocator_failure_releases_chunk),
      cmocka_unit_test(
          test_load_time_allocator_failure_reports_alloc_for_buffer),
      cmocka_unit_test(test_load_time_allocator_failure_reports_alloc_for_file),
      cmocka_unit_test(test_memory_limit_fails_construction),
      cmocka_unit_test(test_runtime_error_reports_traceback),
      cmocka_unit_test(test_load_error_is_recoverable),
      cmocka_unit_test(test_instruction_limit),
      cmocka_unit_test(test_instruction_limit_survives_debug_sethook_attempt),
      cmocka_unit_test(test_instruction_limit_applies_to_coroutine_resume),
      cmocka_unit_test(test_instruction_limit_applies_to_coroutine_wrap),
      cmocka_unit_test(test_debug_sethook_is_disabled),
      cmocka_unit_test(test_package_requires_package_library),
      cmocka_unit_test(test_failed_lua_module_registration_releases_chunk),
      cmocka_unit_test(test_warning_callback_lifetime_and_replacement),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
