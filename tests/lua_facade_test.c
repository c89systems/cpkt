#include <cpkt/lua.h>

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static const char *cpkt_lua_facade_pushvfstring(cpkt_lua_state *state,
                                                const char *format, ...);

static int reject_allocations;

static void *format_test_allocator(void *user, void *pointer, size_t old_size,
                                   size_t new_size) {
  (void)user;
  if (new_size == 0U) {
    free(pointer);
    return NULL;
  }
  if (reject_allocations && new_size > old_size)
    return NULL;
  return realloc(pointer, new_size);
}

static int format_test_pushfstring(cpkt_lua_state *state) {
  reject_allocations = 1;
  (void)cpkt_lua_pushfstring(state, "value=%s", "allocation failure probe");
  return 1;
}

static int format_test_pushfstring_integer(cpkt_lua_state *state) {
  reject_allocations = 1;
  (void)cpkt_lua_pushfstring(state, "value=%I",
                             cpkt_lua_integer_make(0U, 12345U));
  return 1;
}

static int format_test_error_contract(void) {
  cpkt_lua_state *state;
  const char *result;
  int status;

  state = cpkt_lua_newstate(format_test_allocator, NULL, 0U);
  if (state == NULL)
    return 1;
  reject_allocations = 1;
  result = cpkt_lua_facade_pushvfstring(state, "value=%I",
                                        cpkt_lua_integer_make(0U, 12345U));
  reject_allocations = 0;
  if (result != NULL || cpkt_lua_gettop(state) != 1 ||
      strcmp(cpkt_lua_tostring(state, -1), "not enough memory") != 0) {
    cpkt_lua_close(state);
    return 2;
  }
  cpkt_lua_pop(state, 1);

  reject_allocations = 1;
  result = cpkt_lua_facade_pushvfstring(state, "value=%U", 65UL);
  reject_allocations = 0;
  if (result != NULL || cpkt_lua_gettop(state) != 1 ||
      strcmp(cpkt_lua_tostring(state, -1), "not enough memory") != 0) {
    cpkt_lua_close(state);
    return 5;
  }
  cpkt_lua_pop(state, 1);

  cpkt_lua_pushcfunction(state, format_test_pushfstring);
  status = cpkt_lua_pcall(state, 0, 1, 0);
  reject_allocations = 0;
  if (status != CPKT_LUA_ERRMEM) {
    cpkt_lua_close(state);
    return 3;
  }
  cpkt_lua_pop(state, 1);

  cpkt_lua_pushcfunction(state, format_test_pushfstring_integer);
  status = cpkt_lua_pcall(state, 0, 1, 0);
  reject_allocations = 0;
  cpkt_lua_close(state);
  return status == CPKT_LUA_ERRMEM ? 0 : 4;
}

static int cpkt_lua_facade_check_integer(cpkt_lua_integer value,
                                         unsigned int high, unsigned int low) {
  return cpkt_lua_integer_high(value) == high &&
         cpkt_lua_integer_low(value) == low;
}

static const char *cpkt_lua_facade_pushvfstring(cpkt_lua_state *state,
                                                const char *format, ...) {
  va_list arguments;
  const char *result;

  va_start(arguments, format);
  result = cpkt_lua_pushvfstring(state, format, arguments);
  va_end(arguments);
  return result;
}

static int cpkt_lua_facade_error_with_location(cpkt_lua_state *state) {
  return cpkt_lua_l_error(state, "failure %I", cpkt_lua_integer_make(0U, 7U));
}

int main(void) {
  cpkt_lua_buffer buffer;
  cpkt_lua_integer value;
  cpkt_lua_state *state;
  const char *string;

  state = cpkt_lua_l_newstate();
  if (state == 0) {
    return 1;
  }
  cpkt_lua_l_checkversion(state);
  if (cpkt_lua_l_loadstring(state, "return 2 + 3") != CPKT_LUA_OK ||
      cpkt_lua_pcall(state, 0, 1, 0) != CPKT_LUA_OK ||
      !cpkt_lua_facade_check_integer(cpkt_lua_tointeger(state, -1), 0U, 5U)) {
    cpkt_lua_close(state);
    return 2;
  }
  cpkt_lua_pop(state, 1);

  string = cpkt_lua_facade_pushvfstring(state, "%I:%U",
                                        cpkt_lua_integer_make(0U, 1U), 65UL);
  if (string == 0 || strcmp(string, "1:A") != 0) {
    cpkt_lua_close(state);
    return 8;
  }
  cpkt_lua_pop(state, 1);

  string = cpkt_lua_pushfstring(state, "%I", cpkt_lua_integer_make(0U, 1U));
  if (string == 0 || strcmp(string, "1") != 0) {
    cpkt_lua_close(state);
    return 7;
  }
  cpkt_lua_pop(state, 1);

  string = cpkt_lua_pushfstring(state, "%U %q", 65UL);
  if (string == 0 || strcmp(string, "A %q") != 0 ||
      cpkt_lua_gettop(state) != 1) {
    cpkt_lua_close(state);
    return 9;
  }
  cpkt_lua_pop(state, 1);

  string = cpkt_lua_facade_pushvfstring(state, "%I %q",
                                        cpkt_lua_integer_make(0U, 1U));
  if (string == 0 || strcmp(string, "1 %q") != 0 ||
      cpkt_lua_gettop(state) != 1) {
    cpkt_lua_close(state);
    return 10;
  }
  cpkt_lua_pop(state, 1);

  cpkt_lua_pushcfunction(state, cpkt_lua_facade_error_with_location);
  cpkt_lua_setglobal(state, "fail");
  if (cpkt_lua_l_loadbuffer(state, "fail()", 6U, "@example.lua") !=
          CPKT_LUA_OK ||
      cpkt_lua_pcall(state, 0, 0, 0) != CPKT_LUA_ERRRUN) {
    cpkt_lua_close(state);
    return 11;
  }
  string = cpkt_lua_tostring(state, -1);
  if (string == 0 || strcmp(string, "example.lua:1: failure 7") != 0 ||
      cpkt_lua_gettop(state) != 1) {
    cpkt_lua_close(state);
    return 12;
  }
  cpkt_lua_pop(state, 1);

  cpkt_lua_newtable(state);
  cpkt_lua_pushinteger(state, cpkt_lua_integer_make(0U, 9U));
  cpkt_lua_rawseti(state, -2, cpkt_lua_integer_make(0U, 1U));
  if (!cpkt_lua_facade_check_integer(cpkt_lua_rawlen(state, -1), 0U, 1U)) {
    cpkt_lua_close(state);
    return 3;
  }
  cpkt_lua_pop(state, 1);

  cpkt_lua_l_buffinit(state, &buffer);
  cpkt_lua_l_addchar(&buffer, 'a');
  cpkt_lua_l_addstring(&buffer, "bc");
  cpkt_lua_l_pushresult(&buffer);
  string = cpkt_lua_tostring(state, -1);
  if (string == 0 || strcmp(string, "abc") != 0) {
    cpkt_lua_close(state);
    return 4;
  }
  cpkt_lua_pop(state, 1);

  value = cpkt_lua_l_integer_add(cpkt_lua_integer_make(0U, 1U),
                                 cpkt_lua_integer_make(0U, 2U));
  if (!cpkt_lua_facade_check_integer(value, 0U, 3U) ||
      !cpkt_lua_facade_check_integer(
          cpkt_lua_l_integer_shift_left(cpkt_lua_integer_make(0U, 1U), 32U), 1U,
          0U) ||
      !cpkt_lua_facade_check_integer(
          cpkt_lua_l_integer_shift_right(cpkt_lua_integer_make(1U, 0U), 32U),
          0U, 1U) ||
      cpkt_lua_ident() == 0) {
    cpkt_lua_close(state);
    return 5;
  }

  if (cpkt_lua_gc(state, CPKT_LUA_GCGEN) != CPKT_LUA_GCINC ||
      cpkt_lua_gc(state, CPKT_LUA_GCINC) != CPKT_LUA_GCGEN ||
      cpkt_lua_gc(state, CPKT_LUA_GCSTEP, (size_t)INT_MAX + 1U) < 0) {
    cpkt_lua_close(state);
    return 6;
  }

  cpkt_lua_l_openlibs(state);
  cpkt_lua_close(state);
  return format_test_error_contract();
}
