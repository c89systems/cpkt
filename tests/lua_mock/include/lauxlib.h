#ifndef CPKT_TEST_LAUXLIB_H
#define CPKT_TEST_LAUXLIB_H

#include <lua.h>

void luaL_openlibs(lua_State *state);
void luaL_requiref(lua_State *state, const char *name, lua_CFunction opener,
                   int global);
void luaL_traceback(lua_State *state, lua_State *from, const char *message,
                    int level);
int luaL_loadbuffer(lua_State *state, const char *source, size_t size,
                    const char *name);
int luaL_loadfile(lua_State *state, const char *path);

#endif
