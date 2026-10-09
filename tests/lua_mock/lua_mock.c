#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum mock_value_type {
  MOCK_NIL,
  MOCK_TABLE,
  MOCK_FUNCTION,
  MOCK_THREAD,
  MOCK_LIGHTUSERDATA,
  MOCK_USERDATA,
  MOCK_STRING
};

struct mock_value {
  enum mock_value_type type;
  void *ptr;
  const char *text;
  lua_CFunction fn;
};

struct mock_block {
  void *ptr;
  size_t size;
  struct mock_block *next;
};

struct lua_State {
  void *runtime_context;
  lua_CFunction protected_pcall;
  lua_CFunction protected_xpcall;
  lua_Alloc alloc_fn;
  void *alloc_user;
  lua_WarnFunction warn_fn;
  void *warn_user;
  struct mock_block *blocks;
  struct mock_value stack[128];
  int top;
  void *registry_key;
  void *registry_value;
  int package_open;
  int require_open;
  int next_load_error;
  int next_runtime_error;
  int next_limit_loop;
  int raised_error;
  void (*hook)(lua_State *, lua_Debug *);
  int hook_count;
  char message[512];
  char path[256];
  char cpath[256];
};

static int mock_contains(const char *value, size_t size, const char *needle) {
  size_t needle_size;
  size_t i;

  if (value == 0 || needle == 0) {
    return 0;
  }
  needle_size = strlen(needle);
  if (needle_size == 0 || needle_size > size) {
    return 0;
  }
  for (i = 0; i + needle_size <= size; ++i) {
    if (memcmp(value + i, needle, needle_size) == 0) {
      return 1;
    }
  }
  return 0;
}

static struct mock_value *mock_at(lua_State *state, int index) {
  int resolved;

  if (state == 0) {
    return 0;
  }
  if (index < 0) {
    resolved = state->top + index + 1;
  } else {
    resolved = index;
  }
  if (resolved < 1 || resolved > state->top) {
    return 0;
  }
  return &state->stack[resolved - 1];
}

static void mock_push(lua_State *state, enum mock_value_type type) {
  if (state == 0 ||
      state->top >= (int)(sizeof(state->stack) / sizeof(state->stack[0]))) {
    return;
  }
  memset(&state->stack[state->top], 0, sizeof(state->stack[state->top]));
  state->stack[state->top].type = type;
  state->top += 1;
}

static void *mock_alloc_block(lua_State *state, size_t size) {
  struct mock_block *block;
  void *ptr;

  if (size == 0) {
    size = 1;
  }
  ptr = state->alloc_fn(state->alloc_user, 0, 0, size);
  if (ptr == 0) {
    return 0;
  }
  block = (struct mock_block *)state->alloc_fn(state->alloc_user, 0, 0,
                                               sizeof(*block));
  if (block == 0) {
    state->alloc_fn(state->alloc_user, ptr, size, 0);
    return 0;
  }
  block->ptr = ptr;
  block->size = size;
  block->next = state->blocks;
  state->blocks = block;
  return ptr;
}

static void mock_push_message(lua_State *state, const char *message) {
  snprintf(state->message, sizeof(state->message), "%s",
           message != 0 ? message : "");
  mock_push(state, MOCK_STRING);
  state->stack[state->top - 1].text = state->message;
}

lua_State *lua_newstate(lua_Alloc alloc_fn, void *user, int seed) {
  lua_State *state;
  void *scratch;

  (void)seed;
  state = (lua_State *)alloc_fn(user, 0, 0, sizeof(*state));
  if (state == 0) {
    return 0;
  }
  memset(state, 0, sizeof(*state));
  state->alloc_fn = alloc_fn;
  state->alloc_user = user;

  scratch = alloc_fn(user, 0, 0, 8);
  if (scratch != 0) {
    scratch = alloc_fn(user, scratch, 8, 16);
    if (scratch != 0) {
      alloc_fn(user, scratch, 16, 0);
    }
  }
  return state;
}

void lua_close(lua_State *state) {
  struct mock_block *block;
  struct mock_block *next;
  lua_Alloc alloc_fn;
  void *alloc_user;

  if (state == 0) {
    return;
  }
  alloc_fn = state->alloc_fn;
  alloc_user = state->alloc_user;
  block = state->blocks;
  while (block != 0) {
    next = block->next;
    alloc_fn(alloc_user, block->ptr, block->size, 0);
    alloc_fn(alloc_user, block, sizeof(*block), 0);
    block = next;
  }
  alloc_fn(alloc_user, state, sizeof(*state), 0);
}

void lua_setwarnf(lua_State *state, lua_WarnFunction callback, void *user) {
  state->warn_fn = callback;
  state->warn_user = user;
}

void lua_pushlightuserdata(lua_State *state, void *value) {
  mock_push(state, MOCK_LIGHTUSERDATA);
  state->stack[state->top - 1].ptr = value;
}

void lua_gettable(lua_State *state, int index) {
  struct mock_value *key;

  (void)index;
  key = mock_at(state, -1);
  if (key != 0 && key->type == MOCK_LIGHTUSERDATA &&
      key->ptr == state->registry_key) {
    lua_pop(state, 1);
    lua_pushlightuserdata(state, state->registry_value);
  } else {
    lua_pop(state, 1);
    mock_push(state, MOCK_NIL);
  }
}

void *lua_getextraspace(lua_State *state) { return &state->runtime_context; }

lua_CFunction lua_tocfunction(lua_State *state, int index) {
  struct mock_value *value;
  value = mock_at(state, index);
  return value != NULL && value->type == MOCK_FUNCTION ? value->fn : NULL;
}

const char *lua_getupvalue(lua_State *state, int index, int upvalue) {
  (void)state;
  (void)index;
  (void)upvalue;
  return NULL;
}

static int mock_protected_call(lua_State *state) { return lua_gettop(state); }

void lua_setglobal(lua_State *state, const char *name) {
  if (strcmp(name, "pcall") == 0) {
    state->protected_pcall = lua_tocfunction(state, -1);
  } else if (strcmp(name, "xpcall") == 0) {
    state->protected_xpcall = lua_tocfunction(state, -1);
  }
  lua_pop(state, 1);
}

void *lua_touserdata(lua_State *state, int index) {
  struct mock_value *value;

  value = mock_at(state, index);
  if (value == 0) {
    return 0;
  }
  if (value->type == MOCK_LIGHTUSERDATA || value->type == MOCK_USERDATA) {
    return value->ptr;
  }
  return 0;
}

void lua_pop(lua_State *state, int count) {
  if (state == 0 || count <= 0) {
    return;
  }
  if (count > state->top) {
    state->top = 0;
  } else {
    state->top -= count;
  }
}

void lua_settable(lua_State *state, int index) {
  struct mock_value *key;
  struct mock_value *value;

  (void)index;
  key = mock_at(state, -2);
  value = mock_at(state, -1);
  if (key != 0 && value != 0 && key->type == MOCK_LIGHTUSERDATA) {
    state->registry_key = key->ptr;
    state->registry_value = value->ptr;
  }
  lua_pop(state, 2);
}

void lua_createtable(lua_State *state, int narr, int nrec) {
  (void)narr;
  (void)nrec;
  mock_push(state, MOCK_TABLE);
}

void lua_pushstring(lua_State *state, const char *value) {
  mock_push(state, MOCK_STRING);
  state->stack[state->top - 1].text = value != 0 ? value : "";
}

void lua_rawset(lua_State *state, int index) {
  (void)index;
  lua_pop(state, 2);
}

void lua_rawseti(lua_State *state, int index, int n) {
  (void)index;
  (void)n;
  lua_pop(state, 1);
}

int luaL_error(lua_State *state, const char *message) {
  state->raised_error = 1;
  mock_push_message(state, message);
  return 1;
}

const char *lua_tostring(lua_State *state, int index) {
  struct mock_value *value;

  value = mock_at(state, index);
  if (value == 0 || value->type != MOCK_STRING) {
    return 0;
  }
  return value->text;
}

int lua_type(lua_State *state, int index) {
  struct mock_value *value;
  value = mock_at(state, index);
  if (value != NULL && value->type == MOCK_LIGHTUSERDATA) {
    return LUA_TLIGHTUSERDATA;
  }
  return value != NULL && value->type == MOCK_STRING ? LUA_TSTRING : 0;
}

int lua_gettop(lua_State *state) { return state != 0 ? state->top : 0; }

void lua_pushcfunction(lua_State *state, lua_CFunction fn) {
  mock_push(state, MOCK_FUNCTION);
  state->stack[state->top - 1].fn = fn;
}

void lua_pushvalue(lua_State *state, int index) {
  struct mock_value *value;

  value = mock_at(state, index);
  if (value == 0) {
    mock_push(state, MOCK_NIL);
    return;
  }
  mock_push(state, value->type);
  state->stack[state->top - 1] = *value;
}

void lua_insert(lua_State *state, int index) {
  struct mock_value value;
  int i;

  if (state == 0 || index < 1 || index > state->top) {
    return;
  }
  value = state->stack[state->top - 1];
  for (i = state->top - 1; i >= index; --i) {
    state->stack[i] = state->stack[i - 1];
  }
  state->stack[index - 1] = value;
}

int lua_pcall(lua_State *state, int nargs, int nresults, int error_index) {
  lua_Debug debug;
  struct mock_value function;
  struct mock_value results[128];
  int function_index;
  int i;
  int produced;

  (void)nresults;
  (void)error_index;
  if (state->next_limit_loop && state->hook != 0 && state->hook_count > 0) {
    memset(&debug, 0, sizeof(debug));
    do {
      state->hook(state, &debug);
    } while (!state->raised_error);
    state->raised_error = 0;
    state->next_limit_loop = 0;
    mock_push_message(state, "Lua instruction limit exceeded");
    return 1;
  }
  if (state->next_runtime_error) {
    state->next_runtime_error = 0;
    mock_push_message(state, "mock runtime error\nstack traceback");
    return 1;
  }
  function_index = state->top - nargs;
  if (function_index >= 1 && function_index <= state->top) {
    function = state->stack[function_index - 1];
    if (function.type == MOCK_FUNCTION && function.fn != 0) {
      state->raised_error = 0;
      for (i = 0; i < nargs; ++i) {
        state->stack[i] = state->stack[function_index + i];
      }
      state->top = nargs;
      produced = function.fn(state);
      if (state->raised_error) {
        state->raised_error = 0;
        return 1;
      }
      if (produced > state->top) {
        produced = state->top;
      }
      if (produced < 0) {
        produced = 0;
      }
      for (i = 0; i < produced; ++i) {
        results[i] = state->stack[state->top - produced + i];
      }
      for (i = 0; i < produced; ++i) {
        state->stack[i] = results[i];
      }
      state->top = produced;
      return LUA_OK;
    }
  }
  if (state->top > 0) {
    state->top = 1;
    state->stack[0].type = MOCK_STRING;
    state->stack[0].text = "mock result";
  }
  return LUA_OK;
}

void lua_settop(lua_State *state, int index) {
  if (state == 0) {
    return;
  }
  if (index < 0) {
    index = state->top + index + 1;
  }
  if (index < 0) {
    index = 0;
  }
  if (index < state->top) {
    state->top = index;
  } else {
    while (state->top < index) {
      mock_push(state, MOCK_NIL);
    }
  }
}

void lua_remove(lua_State *state, int index) {
  int resolved;
  int i;

  if (state == 0) {
    return;
  }
  resolved = index < 0 ? state->top + index + 1 : index;
  if (resolved < 1 || resolved > state->top) {
    return;
  }
  for (i = resolved - 1; i + 1 < state->top; ++i) {
    state->stack[i] = state->stack[i + 1];
  }
  state->top -= 1;
}

void lua_call(lua_State *state, int nargs, int nresults) {
  (void)nargs;
  (void)nresults;
  if (state->top > 0) {
    state->stack[state->top - 1].type = MOCK_STRING;
    state->stack[state->top - 1].text = "module";
  }
}

void lua_getglobal(lua_State *state, const char *name) {
  if (strcmp(name, "pcall") == 0 || strcmp(name, "xpcall") == 0) {
    lua_CFunction fn;
    fn = strcmp(name, "pcall") == 0 ? state->protected_pcall
                                    : state->protected_xpcall;
    lua_pushcfunction(state, fn != NULL ? fn : mock_protected_call);
    return;
  }
  if (strcmp(name, "package") == 0 && state->package_open) {
    mock_push(state, MOCK_TABLE);
    return;
  }
  if (strcmp(name, "require") == 0 && state->require_open) {
    mock_push(state, MOCK_FUNCTION);
    return;
  }
  mock_push(state, MOCK_NIL);
}

int lua_istable(lua_State *state, int index) {
  struct mock_value *value;

  value = mock_at(state, index);
  return value != 0 && value->type == MOCK_TABLE;
}

int lua_isfunction(lua_State *state, int index) {
  struct mock_value *value;

  value = mock_at(state, index);
  return value != 0 && value->type == MOCK_FUNCTION;
}

void lua_setfield(lua_State *state, int index, const char *name) {
  const char *text;

  (void)index;
  text = lua_tostring(state, -1);
  if (strcmp(name, "path") == 0 && text != 0) {
    snprintf(state->path, sizeof(state->path), "%s", text);
  } else if (strcmp(name, "cpath") == 0 && text != 0) {
    snprintf(state->cpath, sizeof(state->cpath), "%s", text);
  }
  lua_pop(state, 1);
}

void lua_getfield(lua_State *state, int index, const char *name) {
  (void)index;
  if (strcmp(name, "preload") == 0 && state->package_open) {
    mock_push(state, MOCK_TABLE);
  } else if (strcmp(name, "path") == 0) {
    lua_pushstring(state, state->path);
  } else if (strcmp(name, "cpath") == 0) {
    lua_pushstring(state, state->cpath);
  } else {
    mock_push(state, MOCK_NIL);
  }
}

void lua_pushfstring(lua_State *state, const char *fmt, const char *a,
                     const char *b) {
  snprintf(state->message, sizeof(state->message), fmt, a, b);
  lua_pushstring(state, state->message);
}

void *lua_newuserdata(lua_State *state, size_t size) {
  void *ptr;

  ptr = mock_alloc_block(state, size);
  if (ptr == 0) {
    mock_push(state, MOCK_NIL);
    return 0;
  }
  mock_push(state, MOCK_USERDATA);
  state->stack[state->top - 1].ptr = ptr;
  return ptr;
}

void lua_pushcclosure(lua_State *state, lua_CFunction fn, int n) {
  (void)n;
  lua_pushcfunction(state, fn);
}

int lua_error(lua_State *state) {
  if (state != 0) {
    state->raised_error = 1;
  }
  return 1;
}

void lua_sethook(lua_State *state, void (*hook)(lua_State *, lua_Debug *),
                 int mask, int count) {
  (void)mask;
  state->hook = hook;
  state->hook_count = count;
}

lua_State *lua_tothread(lua_State *state, int index) {
  struct mock_value *value;

  value = mock_at(state, index);
  if (value == 0 || value->type != MOCK_THREAD) {
    return 0;
  }
  return (lua_State *)value->ptr;
}

void lua_xmove(lua_State *from, lua_State *to, int n) {
  int i;

  if (from == 0 || to == 0 || n <= 0 || n > from->top) {
    return;
  }
  for (i = from->top - n; i < from->top; ++i) {
    if (to->top < (int)(sizeof(to->stack) / sizeof(to->stack[0]))) {
      to->stack[to->top] = from->stack[i];
      to->top += 1;
    }
  }
  from->top -= n;
}

int lua_resume(lua_State *state, lua_State *from, int nargs, int *nresults) {
  (void)from;
  (void)nargs;
  if (nresults != 0) {
    *nresults = 0;
  }
  if (state != 0 && state->hook != 0 && state->hook_count > 0) {
    lua_Debug debug;

    memset(&debug, 0, sizeof(debug));
    state->hook(state, &debug);
    mock_push_message(state, "Lua instruction limit exceeded");
    if (nresults != 0) {
      *nresults = 1;
    }
    return 2;
  }
  return LUA_OK;
}

void lua_pushboolean(lua_State *state, int value) {
  (void)value;
  mock_push(state, MOCK_STRING);
  state->stack[state->top - 1].text = "boolean";
}

void lua_pushnumber(lua_State *state, lua_Number value) {
  (void)value;
  mock_push(state, MOCK_STRING);
  state->stack[state->top - 1].text = "number";
}

void lua_pushinteger(lua_State *state, lua_Integer value) {
  (void)value;
  mock_push(state, MOCK_STRING);
  state->stack[state->top - 1].text = "integer";
}

void luaL_openlibs(lua_State *state) {
  state->package_open = 1;
  state->require_open = 1;
}

void luaL_requiref(lua_State *state, const char *name, lua_CFunction opener,
                   int global) {
  (void)opener;
  (void)global;
  if (strcmp(name, LUA_LOADLIBNAME) == 0) {
    state->package_open = 1;
    state->require_open = 1;
  }
  mock_push(state, MOCK_TABLE);
}

void luaL_traceback(lua_State *state, lua_State *from, const char *message,
                    int level) {
  (void)from;
  (void)level;
  snprintf(state->message, sizeof(state->message), "%s\nstack traceback",
           message);
  lua_pushstring(state, state->message);
}

int luaL_loadbuffer(lua_State *state, const char *source, size_t size,
                    const char *name) {
  void *prototype;

  if (mock_contains(source, size, "load-error")) {
    snprintf(state->message, sizeof(state->message), "%s: mock load error",
             name);
    lua_pushstring(state, state->message);
    return 1;
  }
  prototype = mock_alloc_block(state, 16);
  if (prototype == 0) {
    mock_push_message(state, "not enough memory");
    return 1;
  }
  state->next_runtime_error = mock_contains(source, size, "runtime-error");
  state->next_limit_loop = mock_contains(source, size, "while true");
  mock_push(state, MOCK_FUNCTION);
  return LUA_OK;
}

int luaL_loadfile(lua_State *state, const char *path) {
  void *prototype;

  if (path != 0 && strstr(path, "load-error") != 0) {
    snprintf(state->message, sizeof(state->message), "%s: mock load error",
             path);
    lua_pushstring(state, state->message);
    return 1;
  }
  prototype = mock_alloc_block(state, 16);
  if (prototype == 0) {
    mock_push_message(state, "not enough memory");
    return 1;
  }
  state->next_runtime_error = path != 0 && strstr(path, "runtime-error") != 0;
  mock_push(state, MOCK_FUNCTION);
  return LUA_OK;
}

int luaopen_base(lua_State *state) {
  (void)state;
  return 1;
}

int luaopen_package(lua_State *state) {
  state->package_open = 1;
  state->require_open = 1;
  return 1;
}

int luaopen_coroutine(lua_State *state) {
  (void)state;
  return 1;
}

int luaopen_table(lua_State *state) {
  (void)state;
  return 1;
}

int luaopen_io(lua_State *state) {
  (void)state;
  return 1;
}

int luaopen_os(lua_State *state) {
  (void)state;
  return 1;
}

int luaopen_string(lua_State *state) {
  (void)state;
  return 1;
}

int luaopen_utf8(lua_State *state) {
  (void)state;
  return 1;
}

int luaopen_math(lua_State *state) {
  (void)state;
  return 1;
}

int luaopen_debug(lua_State *state) {
  (void)state;
  return 1;
}
