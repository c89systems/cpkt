#include <cpkt/lua_runtime.h>

#include <stdlib.h>
#include <string.h>

int cpkt_lua_runtime_example_open(void *lua_state);

struct cpkt_example_allocator {
  size_t alloc_calls;
  size_t realloc_calls;
  size_t free_calls;
  size_t bytes_live;
};

static void *cpkt_example_alloc(void *user, size_t size) {
  struct cpkt_example_allocator *allocator;
  void *ptr;

  allocator = (struct cpkt_example_allocator *)user;
  allocator->alloc_calls += 1;
  ptr = malloc(size);
  if (ptr != 0) {
    allocator->bytes_live += size;
  }
  return ptr;
}

static void *cpkt_example_realloc(void *user, void *ptr, size_t old_size,
                                  size_t new_size) {
  struct cpkt_example_allocator *allocator;
  void *next;

  allocator = (struct cpkt_example_allocator *)user;
  allocator->realloc_calls += 1;
  next = realloc(ptr, new_size);
  if (next != 0) {
    if (allocator->bytes_live >= old_size) {
      allocator->bytes_live -= old_size;
    } else {
      allocator->bytes_live = 0;
    }
    allocator->bytes_live += new_size;
  }
  return next;
}

static void cpkt_example_free(void *user, void *ptr, size_t size) {
  struct cpkt_example_allocator *allocator;

  allocator = (struct cpkt_example_allocator *)user;
  allocator->free_calls += 1;
  if (allocator->bytes_live >= size) {
    allocator->bytes_live -= size;
  } else {
    allocator->bytes_live = 0;
  }
  free(ptr);
}

static int cpkt_example_ok(cpkt_lua_runtime_status status) {
  return status == CPKT_LUA_RUNTIME_OK ? 0 : 1;
}

int main(int argc, char **argv) {
  static const unsigned char preload_source[] = "return {value = 'chunk'}";
  static const unsigned char side_effect_source[] =
      "required_side_effect = 'ok'; return true";
  static const unsigned char buffer_source[] =
      "local host = require('example_host')\n"
      "local chunk = require('example_chunk')\n"
      "if host.context ~= 'example-context' then error('bad context') end\n"
      "if chunk.value ~= 'chunk' then error('bad chunk') end\n"
      "if arg[0] ~= 'buffer.lua' then error('bad arg0') end\n"
      "if arg[1] ~= 'one' or arg[2] ~= 'two' then error('bad argv') end\n";
  static const unsigned char limit_source[] = "while true do end";
  const char *script_argv[2];
  struct cpkt_example_allocator allocator;
  cpkt_lua_runtime_allocator_config allocator_config;
  cpkt_lua_runtime *runtime;
  cpkt_lua_runtime *limited_runtime;
  cpkt_lua_runtime_status status;
  const char *limit_error;

  if (argc != 2) {
    return 2;
  }
  if (cpkt_lua_runtime_lua_version() == 0 ||
      cpkt_lua_runtime_facade_version() == 0) {
    return 3;
  }

  memset(&allocator, 0, sizeof(allocator));
  memset(&allocator_config, 0, sizeof(allocator_config));
  allocator_config.user = &allocator;
  allocator_config.alloc_fn = cpkt_example_alloc;
  allocator_config.realloc_fn = cpkt_example_realloc;
  allocator_config.free_fn = cpkt_example_free;
  allocator_config.max_bytes = 16 * 1024 * 1024;
  status = cpkt_lua_runtime_new_with_allocator(&runtime, &allocator_config);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return 4;
  }

  cpkt_lua_runtime_set_context(runtime, (void *)"example-context");
  if (cpkt_example_ok(cpkt_lua_runtime_open_libs(
          runtime, CPKT_LUA_RUNTIME_LIB_BASE | CPKT_LUA_RUNTIME_LIB_PACKAGE |
                       CPKT_LUA_RUNTIME_LIB_TABLE |
                       CPKT_LUA_RUNTIME_LIB_STRING |
                       CPKT_LUA_RUNTIME_LIB_MATH)) != 0) {
    cpkt_lua_runtime_free(runtime);
    return 5;
  }
  if (cpkt_lua_runtime_context(runtime) == 0) {
    cpkt_lua_runtime_free(runtime);
    return 6;
  }
  if (cpkt_example_ok(cpkt_lua_runtime_set_traceback(runtime, 1)) != 0) {
    cpkt_lua_runtime_free(runtime);
    return 7;
  }
  if (cpkt_example_ok(
          cpkt_lua_runtime_set_package_path(runtime, "first/?.lua")) != 0 ||
      cpkt_example_ok(
          cpkt_lua_runtime_prepend_package_path(runtime, "zero/?.lua")) != 0 ||
      cpkt_example_ok(
          cpkt_lua_runtime_set_package_cpath(runtime, "first/?.so")) != 0 ||
      cpkt_example_ok(
          cpkt_lua_runtime_prepend_package_cpath(runtime, "zero/?.so")) != 0) {
    cpkt_lua_runtime_free(runtime);
    return 8;
  }
  if (cpkt_example_ok(cpkt_lua_runtime_set_global_string(
          runtime, "example_name", "facade")) != 0 ||
      cpkt_example_ok(cpkt_lua_runtime_set_global_boolean(
          runtime, "example_flag", 1)) != 0 ||
      cpkt_example_ok(cpkt_lua_runtime_set_global_integer(
          runtime, "example_count", 7)) != 0 ||
      cpkt_example_ok(cpkt_lua_runtime_set_global_number(
          runtime, "example_number", 2.5)) != 0) {
    cpkt_lua_runtime_free(runtime);
    return 9;
  }
  if (cpkt_example_ok(cpkt_lua_runtime_register_c_module(
          runtime, "example_host", cpkt_lua_runtime_example_open)) != 0) {
    cpkt_lua_runtime_free(runtime);
    return 10;
  }
  if (cpkt_example_ok(cpkt_lua_runtime_register_lua_module(
          runtime, "example_chunk", preload_source, sizeof(preload_source) - 1,
          "example_chunk.lua")) != 0) {
    cpkt_lua_runtime_free(runtime);
    return 11;
  }
  if (cpkt_example_ok(cpkt_lua_runtime_register_lua_module(
          runtime, "example_side_effect", side_effect_source,
          sizeof(side_effect_source) - 1, "example_side_effect.lua")) != 0) {
    cpkt_lua_runtime_free(runtime);
    return 12;
  }
  if (cpkt_example_ok(
          cpkt_lua_runtime_require(runtime, "example_side_effect")) != 0) {
    cpkt_lua_runtime_free(runtime);
    return 13;
  }

  script_argv[0] = "one";
  script_argv[1] = "two";
  if (cpkt_example_ok(cpkt_lua_runtime_run_buffer(
          runtime, buffer_source, sizeof(buffer_source) - 1, "buffer.lua", 2,
          script_argv, 0)) != 0) {
    cpkt_lua_runtime_free(runtime);
    return 14;
  }
  if (cpkt_example_ok(cpkt_lua_runtime_run_file(runtime, argv[1], 2,
                                                script_argv, 0)) != 0) {
    cpkt_lua_runtime_free(runtime);
    return 15;
  }

  cpkt_lua_runtime_free(runtime);
  if (allocator.alloc_calls == 0 || allocator.free_calls == 0) {
    return 16;
  }

  status = cpkt_lua_runtime_new(&limited_runtime);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return 17;
  }
  if (cpkt_example_ok(cpkt_lua_runtime_set_traceback(limited_runtime, 1)) !=
          0 ||
      cpkt_example_ok(
          cpkt_lua_runtime_set_instruction_limit(limited_runtime, 1000)) != 0) {
    cpkt_lua_runtime_free(limited_runtime);
    return 18;
  }
  status = cpkt_lua_runtime_run_buffer(limited_runtime, limit_source,
                                       sizeof(limit_source) - 1, "limit.lua", 0,
                                       0, 0);
  if (status != CPKT_LUA_RUNTIME_ERR_LIMIT) {
    cpkt_lua_runtime_free(limited_runtime);
    return 19;
  }
  limit_error = cpkt_lua_runtime_error(limited_runtime);
  if (limit_error == 0 || strstr(limit_error, "instruction limit") == 0) {
    cpkt_lua_runtime_free(limited_runtime);
    return 20;
  }
  if (cpkt_example_ok(
          cpkt_lua_runtime_clear_instruction_limit(limited_runtime)) != 0) {
    cpkt_lua_runtime_free(limited_runtime);
    return 21;
  }
  cpkt_lua_runtime_clear_error(limited_runtime);
  if (cpkt_lua_runtime_error(limited_runtime)[0] != '\0') {
    cpkt_lua_runtime_free(limited_runtime);
    return 22;
  }
  cpkt_lua_runtime_free(limited_runtime);

  return 0;
}
