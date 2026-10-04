#include <stddef.h>
#include <stdint.h>

#include <cpkt/lua_runtime.h>

int cpkt_fuzz_one_input(const uint8_t *data, size_t size) {
  cpkt_lua_runtime *runtime;

  runtime = NULL;
  if (cpkt_lua_runtime_new_with_limit(&runtime, 256 * 1024) !=
      CPKT_LUA_RUNTIME_OK) {
    return 0;
  }
  (void)cpkt_lua_runtime_open_libs(
      runtime, CPKT_LUA_RUNTIME_LIB_BASE | CPKT_LUA_RUNTIME_LIB_STRING |
                   CPKT_LUA_RUNTIME_LIB_TABLE | CPKT_LUA_RUNTIME_LIB_MATH);
  (void)cpkt_lua_runtime_set_instruction_limit(runtime, 10000);
  (void)cpkt_lua_runtime_run_buffer(runtime, (const unsigned char *)data, size,
                                    "fuzz.lua", 0, NULL, 0);
  cpkt_lua_runtime_free(runtime);
  return 0;
}
