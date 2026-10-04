#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

int main(void) {
  lua_State *state = luaL_newstate();
  if (state == 0) {
    return 1;
  }
  luaL_openlibs(state);
  lua_close(state);
  return 0;
}
