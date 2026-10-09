#include <cpkt/lua.h>

int main(void) {
  cpkt_lua_integer value;
  cpkt_lua_state *state;

  state = cpkt_lua_l_newstate();
  if (state == 0) {
    return 1;
  }
  cpkt_lua_l_checkversion(state);
  cpkt_lua_pushinteger(state, cpkt_lua_integer_make(0U, 7U));
  value = cpkt_lua_tointeger(state, -1);
  cpkt_lua_close(state);
  return cpkt_lua_integer_low(value) == 7U ? 0 : 2;
}
