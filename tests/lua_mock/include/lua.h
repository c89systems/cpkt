#ifndef CPKT_TEST_LUA_H
#define CPKT_TEST_LUA_H

#include <stddef.h>

#define LUA_VERSION "Lua 5.5.0-mock"
#define LUA_OK 0
#define LUA_YIELD 1
#define LUA_MULTRET (-1)
#define LUA_REGISTRYINDEX (-10000)
#define LUA_MASKCOUNT 1
#define LUA_TLIGHTUSERDATA 2
#define LUA_TSTRING 4
#define lua_islightuserdata(L, n) (lua_type((L), (n)) == LUA_TLIGHTUSERDATA)

#define LUA_LOADLIBNAME "package"
#define LUA_COLIBNAME "coroutine"
#define LUA_TABLIBNAME "table"
#define LUA_IOLIBNAME "io"
#define LUA_OSLIBNAME "os"
#define LUA_STRLIBNAME "string"
#define LUA_UTF8LIBNAME "utf8"
#define LUA_MATHLIBNAME "math"
#define LUA_DBLIBNAME "debug"

#define lua_upvalueindex(i) (LUA_REGISTRYINDEX - (i))

typedef struct lua_State lua_State;
typedef struct lua_Debug lua_Debug;
typedef double lua_Number;
typedef long lua_Integer;
typedef int (*lua_CFunction)(lua_State *state);
typedef void *(*lua_Alloc)(void *user, void *ptr, size_t osize, size_t nsize);
typedef void (*lua_WarnFunction)(void *user, const char *message,
                                 int to_continue);

struct lua_Debug {
  int unused;
};

lua_State *lua_newstate(lua_Alloc alloc_fn, void *user, int seed);
void lua_close(lua_State *state);
void lua_setwarnf(lua_State *state, lua_WarnFunction callback, void *user);
void lua_pushlightuserdata(lua_State *state, void *value);
void lua_gettable(lua_State *state, int index);
void *lua_touserdata(lua_State *state, int index);
void *lua_getextraspace(lua_State *state);
lua_CFunction lua_tocfunction(lua_State *state, int index);
const char *lua_getupvalue(lua_State *state, int index, int upvalue);
void lua_setglobal(lua_State *state, const char *name);
void lua_pop(lua_State *state, int count);
void lua_settable(lua_State *state, int index);
void lua_createtable(lua_State *state, int narr, int nrec);
void lua_pushstring(lua_State *state, const char *value);
void lua_rawset(lua_State *state, int index);
void lua_rawseti(lua_State *state, int index, int n);
int luaL_error(lua_State *state, const char *message);
const char *lua_tostring(lua_State *state, int index);
int lua_type(lua_State *state, int index);
int lua_gettop(lua_State *state);
void lua_pushcfunction(lua_State *state, lua_CFunction fn);
void lua_pushvalue(lua_State *state, int index);
void lua_insert(lua_State *state, int index);
int lua_pcall(lua_State *state, int nargs, int nresults, int error_index);
void lua_settop(lua_State *state, int index);
void lua_remove(lua_State *state, int index);
void lua_call(lua_State *state, int nargs, int nresults);
void lua_getglobal(lua_State *state, const char *name);
int lua_istable(lua_State *state, int index);
int lua_isfunction(lua_State *state, int index);
void lua_setfield(lua_State *state, int index, const char *name);
void lua_getfield(lua_State *state, int index, const char *name);
void lua_pushfstring(lua_State *state, const char *fmt, const char *a,
                     const char *b);
void *lua_newuserdata(lua_State *state, size_t size);
void lua_pushcclosure(lua_State *state, lua_CFunction fn, int n);
int lua_error(lua_State *state);
void lua_sethook(lua_State *state, void (*hook)(lua_State *, lua_Debug *),
                 int mask, int count);
lua_State *lua_tothread(lua_State *state, int index);
void lua_xmove(lua_State *from, lua_State *to, int n);
int lua_resume(lua_State *state, lua_State *from, int nargs, int *nresults);
void lua_pushboolean(lua_State *state, int value);
void lua_pushnumber(lua_State *state, lua_Number value);
void lua_pushinteger(lua_State *state, lua_Integer value);

#endif
