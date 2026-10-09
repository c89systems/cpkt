#include <cpkt/lua_runtime.h>

#include <lua.h>

int cpkt_lua_runtime_example_open(void *lua_state) {
  lua_State *state;
  const char *context;

  state = (lua_State *)lua_state;
  context = (const char *)cpkt_lua_runtime_context_from_state(lua_state);

  lua_newtable(state);
  lua_pushstring(state, context != 0 ? context : "");
  lua_setfield(state, -2, "context");
  return 1;
}
