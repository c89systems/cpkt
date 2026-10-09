#ifndef CPKT_TEST_LUALIB_H
#define CPKT_TEST_LUALIB_H

#include <lua.h>

int luaopen_base(lua_State *state);
int luaopen_package(lua_State *state);
int luaopen_coroutine(lua_State *state);
int luaopen_table(lua_State *state);
int luaopen_io(lua_State *state);
int luaopen_os(lua_State *state);
int luaopen_string(lua_State *state);
int luaopen_utf8(lua_State *state);
int luaopen_math(lua_State *state);
int luaopen_debug(lua_State *state);

#endif
