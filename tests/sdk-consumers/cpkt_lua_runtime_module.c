#include <cpkt/lua_runtime.h>

#include <lua.h>

int cpkt_strict_host_open(void *lua_state) {
  lua_State *state = (lua_State *)lua_state;
  const char *context =
      (const char *)cpkt_lua_runtime_context_from_state(lua_state);

  lua_newtable(state);
  lua_pushstring(state, context != 0 ? context : "");
  lua_setfield(state, -2, "context");
  return 1;
}
