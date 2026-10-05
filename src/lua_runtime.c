#include "cpkt/lua_runtime.h"
#include "lua_runtime_hook_policy.h"

#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

struct cpkt_lua_runtime_chunk {
  char *module_name;
  size_t module_name_size;
  char *chunk_name;
  size_t chunk_name_size;
  unsigned char *source;
  size_t source_size;
  size_t source_alloc_size;
  struct cpkt_lua_runtime_chunk *next;
};

struct cpkt_lua_runtime_allocator {
  cpkt_lua_runtime_allocator_config config;
  size_t used;
  int failed;
};

struct cpkt_lua_runtime_c_module {
  cpkt_lua_runtime_c_module_open_fn opener;
  struct cpkt_lua_runtime_c_module *next;
};

struct cpkt_lua_runtime {
  lua_State *state;
  struct cpkt_lua_runtime_allocator allocator;
  char *last_error;
  void *context;
  cpkt_lua_runtime_warning_fn warning_callback;
  void *warning_context;
  struct cpkt_lua_runtime_chunk *chunks;
  struct cpkt_lua_runtime_c_module *c_modules;
  int traceback_enabled;
  int instruction_limit;
  int instruction_limit_hit;
  int instruction_remaining;
  lua_CFunction native_pcall;
  lua_CFunction native_xpcall;
  lua_CFunction native_create;
  lua_CFunction native_resume;
  lua_CFunction native_close;
  lua_CFunction native_wrap;
  lua_CFunction native_auxwrap;
};

struct cpkt_lua_runtime_open_lib {
  const char *name;
  lua_CFunction opener;
};

typedef int (*cpkt_lua_runtime_protected_fn)(lua_State *state);

struct cpkt_lua_runtime_protected_call {
  cpkt_lua_runtime_protected_fn fn;
  void *context;
};

struct cpkt_lua_runtime_set_arg_context {
  const char *script_name;
  int argc;
  const char *const *argv;
};

struct cpkt_lua_runtime_package_field_context {
  const char *field_name;
  const char *value;
  int prepend;
};

struct cpkt_lua_runtime_global_string_context {
  const char *name;
  const char *value;
};

struct cpkt_lua_runtime_global_boolean_context {
  const char *name;
  int value;
};

struct cpkt_lua_runtime_global_number_context {
  const char *name;
  double value;
};

struct cpkt_lua_runtime_global_integer_context {
  const char *name;
  long value;
};

struct cpkt_lua_runtime_c_module_context {
  const char *module_name;
  struct cpkt_lua_runtime_c_module *module;
};

struct cpkt_lua_runtime_lua_module_context {
  struct cpkt_lua_runtime_chunk *chunk;
};

struct cpkt_lua_runtime_require_context {
  const char *module_name;
};

struct cpkt_lua_runtime_load_file_context {
  struct cpkt_lua_runtime_set_arg_context args;
  const char *path;
};

struct cpkt_lua_runtime_load_buffer_context {
  struct cpkt_lua_runtime_set_arg_context args;
  const unsigned char *source;
  size_t source_size;
  const char *chunk_name;
};

static int cpkt_lua_runtime_c_module_loader(lua_State *state);
static int cpkt_lua_runtime_lua_module_loader(lua_State *state);

static void cpkt_lua_runtime_native_warning(void *context, const char *message,
                                            int to_continue) {
  cpkt_lua_runtime *runtime;
  cpkt_lua_runtime_warning_fn callback;
  void *callback_context;

  runtime = (cpkt_lua_runtime *)context;
  callback = runtime->warning_callback;
  callback_context = runtime->warning_context;
  if (callback != NULL) {
    callback(callback_context, message, to_continue);
  }
}

static void *cpkt_lua_runtime_default_alloc(void *user, size_t size) {
  (void)user;
  return malloc(size);
}

static void *cpkt_lua_runtime_default_realloc(void *user, void *ptr,
                                              size_t old_size,
                                              size_t new_size) {
  (void)user;
  (void)old_size;
  return realloc(ptr, new_size);
}

static void cpkt_lua_runtime_default_free(void *user, void *ptr, size_t size) {
  (void)user;
  (void)size;
  free(ptr);
}

static void
cpkt_lua_runtime_allocator_init(struct cpkt_lua_runtime_allocator *allocator,
                                const cpkt_lua_runtime_allocator_config *config,
                                size_t max_bytes) {
  int use_default;

  memset(allocator, 0, sizeof(*allocator));
  if (config != NULL) {
    allocator->config = *config;
  }
  use_default =
      allocator->config.alloc_fn == NULL && allocator->config.free_fn == NULL;
  if (use_default) {
    allocator->config.alloc_fn = cpkt_lua_runtime_default_alloc;
    allocator->config.realloc_fn = cpkt_lua_runtime_default_realloc;
    allocator->config.free_fn = cpkt_lua_runtime_default_free;
  }
  if (max_bytes != 0) {
    allocator->config.max_bytes = max_bytes;
  }
}

static int cpkt_lua_runtime_allocator_can_grow(
    struct cpkt_lua_runtime_allocator *allocator, size_t old_size,
    size_t new_size) {
  size_t adjusted_used;

  if (allocator == NULL || allocator->config.max_bytes == 0 ||
      new_size <= old_size) {
    return 1;
  }
  adjusted_used = allocator->used;
  if (adjusted_used >= old_size) {
    adjusted_used -= old_size;
  } else {
    adjusted_used = 0;
  }
  if (adjusted_used >= allocator->config.max_bytes) {
    allocator->failed = 1;
    return 0;
  }
  if (new_size > allocator->config.max_bytes - adjusted_used) {
    allocator->failed = 1;
    return 0;
  }
  return 1;
}

static void
cpkt_lua_runtime_allocator_account(struct cpkt_lua_runtime_allocator *allocator,
                                   size_t old_size, size_t new_size) {
  if (allocator == NULL) {
    return;
  }
  if (allocator->used >= old_size) {
    allocator->used -= old_size;
  } else {
    allocator->used = 0;
  }
  allocator->used += new_size;
}

static void *
cpkt_lua_runtime_allocator_alloc(struct cpkt_lua_runtime_allocator *allocator,
                                 size_t size) {
  void *ptr;

  if (size == 0) {
    size = 1;
  }
  if (!cpkt_lua_runtime_allocator_can_grow(allocator, 0, size)) {
    return NULL;
  }
  ptr = allocator->config.alloc_fn(allocator->config.user, size);
  if (ptr == NULL) {
    allocator->failed = 1;
    return NULL;
  }
  cpkt_lua_runtime_allocator_account(allocator, 0, size);
  return ptr;
}

static void *
cpkt_lua_runtime_allocator_realloc(struct cpkt_lua_runtime_allocator *allocator,
                                   void *ptr, size_t old_size,
                                   size_t new_size) {
  void *next;
  size_t copy_size;

  if (new_size == 0) {
    if (ptr == NULL) {
      return NULL;
    }
    allocator->config.free_fn(allocator->config.user, ptr, old_size);
    cpkt_lua_runtime_allocator_account(allocator, old_size, 0);
    return NULL;
  }
  if (ptr == NULL) {
    return cpkt_lua_runtime_allocator_alloc(allocator, new_size);
  }
  if (!cpkt_lua_runtime_allocator_can_grow(allocator, old_size, new_size)) {
    return NULL;
  }
  if (allocator->config.realloc_fn != NULL) {
    next = allocator->config.realloc_fn(allocator->config.user, ptr, old_size,
                                        new_size);
    if (next == NULL) {
      allocator->failed = 1;
      return NULL;
    }
    cpkt_lua_runtime_allocator_account(allocator, old_size, new_size);
    return next;
  }

  next = allocator->config.alloc_fn(allocator->config.user, new_size);
  if (next == NULL) {
    allocator->failed = 1;
    return NULL;
  }
  copy_size = old_size < new_size ? old_size : new_size;
  if (copy_size != 0) {
    memcpy(next, ptr, copy_size);
  }
  allocator->config.free_fn(allocator->config.user, ptr, old_size);
  cpkt_lua_runtime_allocator_account(allocator, old_size, new_size);
  return next;
}

static void
cpkt_lua_runtime_allocator_free(struct cpkt_lua_runtime_allocator *allocator,
                                void *ptr, size_t size) {
  if (allocator == NULL || ptr == NULL) {
    return;
  }
  allocator->config.free_fn(allocator->config.user, ptr, size);
  cpkt_lua_runtime_allocator_account(allocator, size, 0);
}

static void *cpkt_lua_runtime_lua_alloc(void *ud, void *ptr, size_t osize,
                                        size_t nsize) {
  struct cpkt_lua_runtime_allocator *allocator;

  allocator = (struct cpkt_lua_runtime_allocator *)ud;
  return cpkt_lua_runtime_allocator_realloc(allocator, ptr, osize, nsize);
}

static char *cpkt_lua_runtime_strdup(cpkt_lua_runtime *runtime,
                                     const char *value, size_t *out_size) {
  size_t size;
  char *copy;

  if (value == NULL) {
    return NULL;
  }

  size = strlen(value) + 1;
  copy = (char *)cpkt_lua_runtime_allocator_alloc(&runtime->allocator, size);
  if (copy == NULL) {
    return NULL;
  }
  memcpy(copy, value, size);
  if (out_size != NULL) {
    *out_size = size;
  }
  return copy;
}

static void cpkt_lua_runtime_free_chunk(cpkt_lua_runtime *runtime,
                                        struct cpkt_lua_runtime_chunk *chunk) {
  if (runtime == NULL || chunk == NULL) {
    return;
  }
  cpkt_lua_runtime_allocator_free(&runtime->allocator, chunk->module_name,
                                  chunk->module_name_size);
  cpkt_lua_runtime_allocator_free(&runtime->allocator, chunk->chunk_name,
                                  chunk->chunk_name_size);
  cpkt_lua_runtime_allocator_free(&runtime->allocator, chunk->source,
                                  chunk->source_alloc_size);
  cpkt_lua_runtime_allocator_free(&runtime->allocator, chunk, sizeof(*chunk));
}

static void cpkt_lua_runtime_clear_chunks(cpkt_lua_runtime *runtime) {
  struct cpkt_lua_runtime_chunk *chunk;
  struct cpkt_lua_runtime_chunk *next;

  chunk = runtime->chunks;
  while (chunk != NULL) {
    next = chunk->next;
    cpkt_lua_runtime_free_chunk(runtime, chunk);
    chunk = next;
  }
  runtime->chunks = NULL;
}

static cpkt_lua_runtime_status
cpkt_lua_runtime_set_error(cpkt_lua_runtime *runtime,
                           cpkt_lua_runtime_status status,
                           const char *message) {
  char *copy;

  if (runtime == NULL) {
    return status;
  }

  cpkt_lua_runtime_allocator_free(
      &runtime->allocator, runtime->last_error,
      runtime->last_error != NULL ? strlen(runtime->last_error) + 1 : 0);
  runtime->last_error = NULL;

  if (message != NULL) {
    copy = cpkt_lua_runtime_strdup(runtime, message, NULL);
    if (copy == NULL) {
      runtime->last_error =
          cpkt_lua_runtime_strdup(runtime, "out of memory", NULL);
      return CPKT_LUA_RUNTIME_ERR_ALLOC;
    }
    runtime->last_error = copy;
  }

  return status;
}

/* VM extra space belongs to the embedder, unlike Lua's writable registry. */
static cpkt_lua_runtime *cpkt_lua_runtime_from_state(lua_State *state) {
  return *(cpkt_lua_runtime **)lua_getextraspace(state);
}

static void cpkt_lua_runtime_store_state(lua_State *state,
                                         cpkt_lua_runtime *runtime) {
  *(cpkt_lua_runtime **)lua_getextraspace(state) = runtime;
}

static int cpkt_lua_runtime_store_state_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  cpkt_lua_runtime *runtime;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  runtime = call != NULL ? (cpkt_lua_runtime *)call->context : NULL;
  cpkt_lua_runtime_store_state(state, runtime);
  return 0;
}

static void cpkt_lua_runtime_set_arg(
    lua_State *state, const struct cpkt_lua_runtime_set_arg_context *context) {
  int i;

  lua_createtable(state, context->argc, 1);
  lua_pushstring(state,
                 context->script_name != NULL ? context->script_name : "");
  lua_rawseti(state, -2, 0);

  for (i = 0; i < context->argc; ++i) {
    lua_pushstring(state, context->argv[i] != NULL ? context->argv[i] : "");
    lua_rawseti(state, -2, i + 1);
  }

  lua_setglobal(state, "arg");
}

static int cpkt_lua_runtime_call_protected_fn(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  if (call == NULL || call->fn == NULL) {
    return luaL_error(state, "missing protected Lua runtime operation");
  }
  return call->fn(state);
}

static cpkt_lua_runtime_status
cpkt_lua_runtime_maybe_openlibs(cpkt_lua_runtime *runtime, int flags) {
  if ((flags & CPKT_LUA_RUNTIME_OPEN_LIBS) != 0) {
    return cpkt_lua_runtime_openlibs(runtime);
  }
  return CPKT_LUA_RUNTIME_OK;
}

static void cpkt_lua_runtime_instruction_hook(lua_State *state,
                                              lua_Debug *debug) {
  cpkt_lua_runtime *runtime;

  (void)debug;
  runtime = cpkt_lua_runtime_from_state(state);
  if (runtime == NULL || runtime->instruction_limit <= 0) {
    return;
  }
  if (!runtime->instruction_limit_hit && runtime->instruction_remaining > 0) {
    runtime->instruction_remaining -= 1;
    if (runtime->instruction_remaining > 0) {
      return;
    }
  }
  runtime->instruction_limit_hit = 1;
  luaL_error(state, "Lua instruction limit exceeded");
}

static void cpkt_lua_runtime_apply_instruction_hook(cpkt_lua_runtime *runtime,
                                                    lua_State *state) {
  if (runtime == NULL || state == NULL) {
    return;
  }
  if (runtime->instruction_limit > 0) {
    /* One shared counter accounts for every thread, including short yields. */
    lua_sethook(state, cpkt_lua_runtime_instruction_hook,
                LUA_MASKCOUNT | CPKT_LUA_RUNTIME_MASK_FINALIZERS, 1);
  } else {
    lua_sethook(state, NULL, 0, 0);
  }
}

/* Native base functions have no upvalues. Calling them in this same C frame
 * preserves their yielding continuations without exposing a Lua-callable
 * bypass. */
static int cpkt_lua_runtime_guard_protected_call(lua_State *state,
                                                 lua_CFunction native) {
  cpkt_lua_runtime *runtime;
  int results;
  runtime = cpkt_lua_runtime_from_state(state);
  if (runtime == NULL || native == NULL) {
    return luaL_error(state, "missing protected-call runtime");
  }
  if (runtime->instruction_limit_hit) {
    return luaL_error(state, "Lua instruction limit exceeded");
  }
  results = native(state);
  if (runtime->instruction_limit_hit) {
    return luaL_error(state, "Lua instruction limit exceeded");
  }
  return results;
}

static int cpkt_lua_runtime_pcall(lua_State *state) {
  cpkt_lua_runtime *runtime;
  runtime = cpkt_lua_runtime_from_state(state);
  return cpkt_lua_runtime_guard_protected_call(
      state, runtime != NULL ? runtime->native_pcall : NULL);
}

static int cpkt_lua_runtime_xpcall_error(lua_State *state) {
  cpkt_lua_runtime *runtime;
  runtime = cpkt_lua_runtime_from_state(state);
  if (runtime == NULL) {
    return luaL_error(state, "missing protected-call runtime");
  }
  /* A hook error invokes this handler before Lua restores allowhook. Return
   * that error unchanged; the outer guard will terminate the host run. */
  if (runtime->instruction_limit_hit) {
    return 1;
  }
  lua_pushvalue(state, lua_upvalueindex(1));
  lua_insert(state, 1);
  lua_call(state, 1, 1);
  return 1;
}

static int cpkt_lua_runtime_xpcall(lua_State *state) {
  cpkt_lua_runtime *runtime;
  runtime = cpkt_lua_runtime_from_state(state);
  if (runtime != NULL && runtime->instruction_limit > 0 &&
      lua_type(state, 2) == LUA_TFUNCTION) {
    lua_pushvalue(state, 2);
    lua_pushcclosure(state, cpkt_lua_runtime_xpcall_error, 1);
    lua_insert(state, 2);
    lua_remove(state, 3);
  }
  return cpkt_lua_runtime_guard_protected_call(
      state, runtime != NULL ? runtime->native_xpcall : NULL);
}

static int cpkt_lua_runtime_wrap_protected_calls(lua_State *state) {
  cpkt_lua_runtime *runtime;
  lua_CFunction native_pcall;
  lua_CFunction native_xpcall;
  runtime = cpkt_lua_runtime_from_state(state);
  if (runtime == NULL) {
    return luaL_error(state, "missing Lua runtime");
  }
  lua_getglobal(state, "pcall");
  native_pcall = lua_tocfunction(state, -1);
  if (native_pcall == NULL || lua_getupvalue(state, -1, 1) != NULL ||
      (native_pcall != cpkt_lua_runtime_pcall &&
       runtime->native_pcall != NULL &&
       native_pcall != runtime->native_pcall)) {
    return luaL_error(state, "unsupported native pcall implementation");
  }
  lua_pop(state, 1);
  lua_getglobal(state, "xpcall");
  native_xpcall = lua_tocfunction(state, -1);
  if (native_xpcall == NULL || lua_getupvalue(state, -1, 1) != NULL ||
      (native_xpcall != cpkt_lua_runtime_xpcall &&
       runtime->native_xpcall != NULL &&
       native_xpcall != runtime->native_xpcall)) {
    return luaL_error(state, "unsupported native xpcall implementation");
  }
  lua_pop(state, 1);
  if (native_pcall != cpkt_lua_runtime_pcall) {
    runtime->native_pcall = native_pcall;
  }
  if (native_xpcall != cpkt_lua_runtime_xpcall) {
    runtime->native_xpcall = native_xpcall;
  }
  lua_pushcfunction(state, cpkt_lua_runtime_pcall);
  lua_setglobal(state, "pcall");
  lua_pushcfunction(state, cpkt_lua_runtime_xpcall);
  lua_setglobal(state, "xpcall");
  return 0;
}

static int cpkt_lua_runtime_disabled_debug_sethook(lua_State *state) {
  (void)state;
  return luaL_error(
      state, "debug.sethook is unavailable in the cpkt Lua runtime facade");
}

static void cpkt_lua_runtime_disable_debug_sethook(cpkt_lua_runtime *runtime) {
  lua_State *state;

  state = runtime->state;
  lua_getglobal(state, "debug");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_pushcfunction(state, cpkt_lua_runtime_disabled_debug_sethook);
  lua_setfield(state, -2, "sethook");
  lua_pop(state, 1);
}

static int cpkt_lua_runtime_disable_debug_sethook_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  cpkt_lua_runtime *runtime;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  runtime = call != NULL ? (cpkt_lua_runtime *)call->context : NULL;
  if (runtime == NULL) {
    return luaL_error(state, "missing Lua runtime");
  }
  cpkt_lua_runtime_disable_debug_sethook(runtime);
  return 0;
}

static int cpkt_lua_runtime_coroutine_create(lua_State *state) {
  cpkt_lua_runtime *runtime;
  lua_State *coroutine;
  int results;
  runtime = cpkt_lua_runtime_from_state(state);
  if (runtime == NULL || runtime->native_create == NULL) {
    return luaL_error(state, "missing coroutine runtime");
  }
  results = runtime->native_create(state);
  coroutine = lua_tothread(state, -1);
  if (coroutine != NULL) {
    cpkt_lua_runtime_store_state(coroutine, runtime);
    cpkt_lua_runtime_apply_instruction_hook(runtime, coroutine);
  }
  return results;
}

static int cpkt_lua_runtime_coroutine_resume(lua_State *state) {
  cpkt_lua_runtime *runtime;
  lua_State *coroutine;
  runtime = cpkt_lua_runtime_from_state(state);
  if (runtime == NULL || runtime->native_resume == NULL) {
    return luaL_error(state, "missing coroutine runtime");
  }
  coroutine = lua_tothread(state, 1);
  if (coroutine != NULL) {
    cpkt_lua_runtime_store_state(coroutine, runtime);
    cpkt_lua_runtime_apply_instruction_hook(runtime, coroutine);
  }
  return runtime->native_resume(state);
}

static int cpkt_lua_runtime_coroutine_close(lua_State *state) {
  cpkt_lua_runtime *runtime;
  lua_State *coroutine;
  runtime = cpkt_lua_runtime_from_state(state);
  if (runtime == NULL || runtime->native_close == NULL) {
    return luaL_error(state, "missing coroutine runtime");
  }
  coroutine = lua_tothread(state, 1);
  if (coroutine != NULL) {
    cpkt_lua_runtime_store_state(coroutine, runtime);
    cpkt_lua_runtime_apply_instruction_hook(runtime, coroutine);
  }
  return runtime->native_close(state);
}

static int cpkt_lua_runtime_coroutine_auxwrap(lua_State *state) {
  lua_State *coroutine;
  cpkt_lua_runtime *runtime;
  coroutine = lua_tothread(state, lua_upvalueindex(1));
  runtime = cpkt_lua_runtime_from_state(state);
  if (coroutine == NULL || runtime == NULL || runtime->native_auxwrap == NULL) {
    return luaL_error(state, "missing coroutine runtime");
  }
  cpkt_lua_runtime_store_state(coroutine, runtime);
  cpkt_lua_runtime_apply_instruction_hook(runtime, coroutine);
  /* Native auxwrap expects only the coroutine in its first upvalue. */
  return runtime->native_auxwrap(state);
}

static int cpkt_lua_runtime_coroutine_wrap(lua_State *state) {
  cpkt_lua_runtime *runtime;
  lua_State *coroutine;
  lua_CFunction native;
  runtime = cpkt_lua_runtime_from_state(state);
  if (runtime == NULL || runtime->native_wrap == NULL) {
    return luaL_error(state, "missing coroutine runtime");
  }
  runtime->native_wrap(state);
  native = lua_tocfunction(state, -1);
  if (native == NULL || lua_getupvalue(state, -1, 1) == NULL) {
    return luaL_error(state,
                      "unsupported native coroutine.wrap implementation");
  }
  coroutine = lua_tothread(state, -1);
  if (coroutine == NULL ||
      (runtime->native_auxwrap != NULL && runtime->native_auxwrap != native)) {
    return luaL_error(state, "unsupported native coroutine wrapper");
  }
  runtime->native_auxwrap = native;
  cpkt_lua_runtime_store_state(coroutine, runtime);
  cpkt_lua_runtime_apply_instruction_hook(runtime, coroutine);
  /* Retain the thread, never the callable native wrapper, as the upvalue. */
  lua_remove(state, -2);
  lua_pushcclosure(state, cpkt_lua_runtime_coroutine_auxwrap, 1);
  return 1;
}

static void cpkt_lua_runtime_wrap_coroutine_function(
    cpkt_lua_runtime *runtime, const char *name, lua_CFunction wrapper,
    lua_CFunction *native_slot) {
  lua_State *state;
  lua_CFunction native;
  state = runtime->state;
  lua_getfield(state, -1, name);
  native = lua_tocfunction(state, -1);
  if (native == wrapper) {
    lua_pop(state, 1);
    return;
  }
  if (native == NULL || lua_getupvalue(state, -1, 1) != NULL ||
      (*native_slot != NULL && native != *native_slot)) {
    luaL_error(state, "unsupported native coroutine entrypoint");
    return;
  }
  *native_slot = native;
  lua_pop(state, 1);
  lua_pushcfunction(state, wrapper);
  lua_setfield(state, -2, name);
}

static void cpkt_lua_runtime_wrap_coroutine_library(cpkt_lua_runtime *runtime) {
  lua_State *state;

  if (runtime == NULL) {
    return;
  }

  state = runtime->state;
  lua_getglobal(state, "coroutine");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }

  cpkt_lua_runtime_wrap_coroutine_function(runtime, "create",
                                           cpkt_lua_runtime_coroutine_create,
                                           &runtime->native_create);
  cpkt_lua_runtime_wrap_coroutine_function(runtime, "resume",
                                           cpkt_lua_runtime_coroutine_resume,
                                           &runtime->native_resume);
  cpkt_lua_runtime_wrap_coroutine_function(runtime, "close",
                                           cpkt_lua_runtime_coroutine_close,
                                           &runtime->native_close);
  cpkt_lua_runtime_wrap_coroutine_function(
      runtime, "wrap", cpkt_lua_runtime_coroutine_wrap, &runtime->native_wrap);
  lua_pop(state, 1);
}

static int cpkt_lua_runtime_wrap_coroutine_library_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  cpkt_lua_runtime *runtime;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  runtime = call != NULL ? (cpkt_lua_runtime *)call->context : NULL;
  if (runtime == NULL) {
    return luaL_error(state, "missing Lua runtime");
  }
  cpkt_lua_runtime_wrap_coroutine_library(runtime);
  return 0;
}

static int cpkt_lua_runtime_traceback(lua_State *state) {
  const char *message;

  message = lua_tostring(state, 1);
  if (message == NULL) {
    message = "Lua runtime error";
  }
  luaL_traceback(state, state, message, 1);
  return 1;
}

/* Numeric error conversion can allocate; it must remain inside lua_pcall. */
static int cpkt_lua_runtime_error_to_string(lua_State *state) {
  const char *message;
  message = lua_tostring(state, 1);
  if (message == NULL) {
    lua_pushstring(state, "non-string Lua error");
  }
  return 1;
}

static cpkt_lua_runtime_status cpkt_lua_runtime_call_with_status(
    cpkt_lua_runtime *runtime, int nargs, int nresults,
    cpkt_lua_runtime_status failure_status, int traceback_enabled) {
  lua_State *state;
  int function_index;
  int error_index;
  int result;
  cpkt_lua_runtime_status status;

  state = runtime->state;
  function_index = lua_gettop(state) - nargs;
  error_index = 0;
  runtime->instruction_limit_hit = 0;
  runtime->instruction_remaining = runtime->instruction_limit;
  cpkt_lua_runtime_apply_instruction_hook(runtime, state);
  runtime->allocator.failed = 0;

  if (traceback_enabled) {
    lua_pushcfunction(state, cpkt_lua_runtime_traceback);
    lua_insert(state, function_index);
    error_index = function_index;
  }

  result = lua_pcall(state, nargs, nresults, error_index);
  if (result != LUA_OK) {
    if (runtime->instruction_limit_hit) {
      status = CPKT_LUA_RUNTIME_ERR_LIMIT;
    } else if (runtime->allocator.failed) {
      status = CPKT_LUA_RUNTIME_ERR_ALLOC;
    } else {
      status = failure_status;
    }
    if (lua_type(state, -1) != LUA_TSTRING) {
      lua_pushcfunction(state, cpkt_lua_runtime_error_to_string);
      lua_pushvalue(state, -2);
      if (lua_pcall(state, 1, 1, 0) != LUA_OK && runtime->allocator.failed) {
        status = CPKT_LUA_RUNTIME_ERR_ALLOC;
      }
      status =
          cpkt_lua_runtime_set_error(runtime, status, lua_tostring(state, -1));
      lua_pop(state, 1);
    } else {
      status =
          cpkt_lua_runtime_set_error(runtime, status, lua_tostring(state, -1));
    }
    if (error_index != 0) {
      lua_settop(state, error_index - 1);
    } else {
      lua_pop(state, 1);
    }
    return status;
  }

  if (error_index != 0) {
    if (nresults == LUA_MULTRET) {
      lua_settop(state, error_index - 1);
    } else {
      lua_remove(state, error_index);
    }
  }

  runtime->allocator.failed = 0;
  if (runtime->instruction_limit_hit) {
    return cpkt_lua_runtime_set_error(runtime, CPKT_LUA_RUNTIME_ERR_LIMIT,
                                      "Lua instruction limit exceeded");
  }

  cpkt_lua_runtime_clear_error(runtime);
  return CPKT_LUA_RUNTIME_OK;
}

static cpkt_lua_runtime_status cpkt_lua_runtime_call(cpkt_lua_runtime *runtime,
                                                     int nargs, int nresults) {
  return cpkt_lua_runtime_call_with_status(runtime, nargs, nresults,
                                           CPKT_LUA_RUNTIME_ERR_RUNTIME,
                                           runtime->traceback_enabled);
}

static cpkt_lua_runtime_status cpkt_lua_runtime_protected_call(
    cpkt_lua_runtime *runtime, cpkt_lua_runtime_protected_fn fn, void *context,
    int nresults, cpkt_lua_runtime_status failure_status) {
  struct cpkt_lua_runtime_protected_call call;

  call.fn = fn;
  call.context = context;
  lua_pushcfunction(runtime->state, cpkt_lua_runtime_call_protected_fn);
  lua_pushlightuserdata(runtime->state, &call);
  return cpkt_lua_runtime_call_with_status(runtime, 1, nresults, failure_status,
                                           0);
}

static cpkt_lua_runtime_status
cpkt_lua_runtime_disable_debug_sethook_checked(cpkt_lua_runtime *runtime) {
  return cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_disable_debug_sethook_protected, runtime, 0,
      CPKT_LUA_RUNTIME_ERR_RUNTIME);
}

static cpkt_lua_runtime_status
cpkt_lua_runtime_wrap_coroutine_library_checked(cpkt_lua_runtime *runtime) {
  return cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_wrap_coroutine_library_protected, runtime, 0,
      CPKT_LUA_RUNTIME_ERR_RUNTIME);
}

static int cpkt_lua_runtime_open_one_lib_protected(lua_State *state) {
  struct cpkt_lua_runtime_open_lib *lib;

  struct cpkt_lua_runtime_protected_call *call;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  lib = call != NULL ? (struct cpkt_lua_runtime_open_lib *)call->context : NULL;
  if (lib == NULL || lib->name == NULL || lib->opener == NULL) {
    return luaL_error(state, "missing Lua library opener");
  }
  luaL_requiref(state, lib->name, lib->opener, 1);
  return 1;
}

static cpkt_lua_runtime_status
cpkt_lua_runtime_open_one_lib(cpkt_lua_runtime *runtime, const char *name,
                              lua_CFunction opener) {
  struct cpkt_lua_runtime_open_lib lib;
  cpkt_lua_runtime_status status;

  lib.name = name;
  lib.opener = opener;
  status = cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_open_one_lib_protected, &lib, 1,
      CPKT_LUA_RUNTIME_ERR_RUNTIME);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return status;
  }
  lua_pop(runtime->state, 1);
  return CPKT_LUA_RUNTIME_OK;
}

static int cpkt_lua_runtime_set_package_field_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  struct cpkt_lua_runtime_package_field_context *context;
  const char *current;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  context = call != NULL
                ? (struct cpkt_lua_runtime_package_field_context *)call->context
                : NULL;
  if (context == NULL || context->field_name == NULL ||
      context->value == NULL) {
    return luaL_error(state, "missing package field");
  }
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    return luaL_error(
        state,
        "Lua package table is unavailable; open the package library first");
  }
  if (context->prepend) {
    lua_getfield(state, -1, context->field_name);
    current = lua_tostring(state, -1);
    if (current != NULL && current[0] != '\0') {
      lua_pushfstring(state, "%s;%s", context->value, current);
    } else {
      lua_pushstring(state, context->value);
    }
    lua_remove(state, -2);
  } else {
    lua_pushstring(state, context->value);
  }
  lua_setfield(state, -2, context->field_name);
  lua_pop(state, 1);
  return 0;
}

static cpkt_lua_runtime_status
cpkt_lua_runtime_update_package_field(cpkt_lua_runtime *runtime,
                                      const char *field_name, const char *value,
                                      int prepend) {
  struct cpkt_lua_runtime_package_field_context context;

  context.field_name = field_name;
  context.value = value;
  context.prepend = prepend;
  return cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_set_package_field_protected, &context, 0,
      CPKT_LUA_RUNTIME_ERR_RUNTIME);
}

static int cpkt_lua_runtime_set_global_string_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  struct cpkt_lua_runtime_global_string_context *context;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  context = call != NULL
                ? (struct cpkt_lua_runtime_global_string_context *)call->context
                : NULL;
  if (context == NULL || context->name == NULL || context->value == NULL) {
    return luaL_error(state, "missing string global");
  }
  lua_pushstring(state, context->value);
  lua_setglobal(state, context->name);
  return 0;
}

static int cpkt_lua_runtime_set_global_boolean_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  struct cpkt_lua_runtime_global_boolean_context *context;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  context =
      call != NULL
          ? (struct cpkt_lua_runtime_global_boolean_context *)call->context
          : NULL;
  if (context == NULL || context->name == NULL) {
    return luaL_error(state, "missing boolean global");
  }
  lua_pushboolean(state, context->value != 0);
  lua_setglobal(state, context->name);
  return 0;
}

static int cpkt_lua_runtime_set_global_number_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  struct cpkt_lua_runtime_global_number_context *context;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  context = call != NULL
                ? (struct cpkt_lua_runtime_global_number_context *)call->context
                : NULL;
  if (context == NULL || context->name == NULL) {
    return luaL_error(state, "missing number global");
  }
  lua_pushnumber(state, (lua_Number)context->value);
  lua_setglobal(state, context->name);
  return 0;
}

static int cpkt_lua_runtime_set_global_integer_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  struct cpkt_lua_runtime_global_integer_context *context;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  context =
      call != NULL
          ? (struct cpkt_lua_runtime_global_integer_context *)call->context
          : NULL;
  if (context == NULL || context->name == NULL) {
    return luaL_error(state, "missing integer global");
  }
  lua_pushinteger(state, (lua_Integer)context->value);
  lua_setglobal(state, context->name);
  return 0;
}

static int cpkt_lua_runtime_register_c_module_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  struct cpkt_lua_runtime_c_module_context *context;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  context = call != NULL
                ? (struct cpkt_lua_runtime_c_module_context *)call->context
                : NULL;
  if (context == NULL || context->module_name == NULL ||
      context->module == NULL || context->module->opener == NULL) {
    return luaL_error(state, "missing C module registration");
  }
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    return luaL_error(state, "Lua package table is unavailable; open standard "
                             "libraries before registering preload modules");
  }
  lua_getfield(state, -1, "preload");
  lua_remove(state, -2);
  if (!lua_istable(state, -1)) {
    return luaL_error(state, "Lua package.preload table is unavailable");
  }
  lua_pushlightuserdata(state, context->module);
  lua_pushcclosure(state, cpkt_lua_runtime_c_module_loader, 1);
  lua_pushstring(state, context->module_name);
  lua_insert(state, -2);
  lua_rawset(state, -3);
  lua_pop(state, 1);
  return 0;
}

static int cpkt_lua_runtime_register_lua_module_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  struct cpkt_lua_runtime_lua_module_context *context;
  struct cpkt_lua_runtime_chunk *chunk;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  context = call != NULL
                ? (struct cpkt_lua_runtime_lua_module_context *)call->context
                : NULL;
  chunk = context != NULL ? context->chunk : NULL;
  if (chunk == NULL || chunk->module_name == NULL) {
    return luaL_error(state, "missing Lua module registration");
  }
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    return luaL_error(state, "Lua package table is unavailable; open standard "
                             "libraries before registering preload modules");
  }
  lua_getfield(state, -1, "preload");
  lua_remove(state, -2);
  if (!lua_istable(state, -1)) {
    return luaL_error(state, "Lua package.preload table is unavailable");
  }
  lua_pushlightuserdata(state, chunk);
  lua_pushcclosure(state, cpkt_lua_runtime_lua_module_loader, 1);
  lua_pushstring(state, chunk->module_name);
  lua_insert(state, -2);
  /* Publish without invoking a metamethod that could retain the loader and
   * then raise an error before runtime ownership is committed. */
  lua_rawset(state, -3);
  lua_pop(state, 1);
  return 0;
}

static int cpkt_lua_runtime_require_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  struct cpkt_lua_runtime_require_context *context;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  context = call != NULL
                ? (struct cpkt_lua_runtime_require_context *)call->context
                : NULL;
  if (context == NULL || context->module_name == NULL) {
    return luaL_error(state, "missing require module name");
  }
  lua_getglobal(state, "require");
  if (!lua_isfunction(state, -1)) {
    return luaL_error(
        state,
        "Lua require function is unavailable; open the package library first");
  }
  lua_pushstring(state, context->module_name);
  lua_call(state, 1, 1);
  return 1;
}

static int cpkt_lua_runtime_load_file_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  struct cpkt_lua_runtime_load_file_context *context;
  int result;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  context = call != NULL
                ? (struct cpkt_lua_runtime_load_file_context *)call->context
                : NULL;
  if (context == NULL || context->path == NULL) {
    return luaL_error(state, "missing Lua file path");
  }
  cpkt_lua_runtime_set_arg(state, &context->args);
  result = luaL_loadfile(state, context->path);
  if (result != LUA_OK) {
    return lua_error(state);
  }
  return 1;
}

static int cpkt_lua_runtime_load_buffer_protected(lua_State *state) {
  struct cpkt_lua_runtime_protected_call *call;
  struct cpkt_lua_runtime_load_buffer_context *context;
  int result;

  call = (struct cpkt_lua_runtime_protected_call *)lua_touserdata(state, 1);
  context = call != NULL
                ? (struct cpkt_lua_runtime_load_buffer_context *)call->context
                : NULL;
  if (context == NULL || context->source == NULL) {
    return luaL_error(state, "missing Lua source buffer");
  }
  cpkt_lua_runtime_set_arg(state, &context->args);
  result = luaL_loadbuffer(
      state, (const char *)context->source, context->source_size,
      context->chunk_name != NULL ? context->chunk_name : "buffer");
  if (result != LUA_OK) {
    return lua_error(state);
  }
  return 1;
}

static cpkt_lua_runtime_status
cpkt_lua_runtime_set_package_field(cpkt_lua_runtime *runtime,
                                   const char *field_name, const char *value) {
  return cpkt_lua_runtime_update_package_field(runtime, field_name, value, 0);
}

static cpkt_lua_runtime_status cpkt_lua_runtime_prepend_package_field(
    cpkt_lua_runtime *runtime, const char *field_name, const char *value) {
  return cpkt_lua_runtime_update_package_field(runtime, field_name, value, 1);
}

static int cpkt_lua_runtime_c_module_loader(lua_State *state) {
  cpkt_lua_runtime *runtime;
  struct cpkt_lua_runtime_c_module *module;
  void *candidate;

  runtime = cpkt_lua_runtime_from_state(state);
  if (runtime == NULL || !lua_islightuserdata(state, lua_upvalueindex(1))) {
    return luaL_error(state, "invalid C module opener");
  }
  candidate = lua_touserdata(state, lua_upvalueindex(1));
  for (module = runtime->c_modules; module != NULL; module = module->next) {
    if ((void *)module == candidate) {
      return module->opener((void *)state);
    }
  }
  return luaL_error(state, "invalid C module opener");
}

static int cpkt_lua_runtime_lua_module_loader(lua_State *state) {
  struct cpkt_lua_runtime_chunk *chunk;
  cpkt_lua_runtime *runtime;
  void *candidate;
  int result;
  int nargs;

  runtime = cpkt_lua_runtime_from_state(state);
  if (runtime == NULL || !lua_islightuserdata(state, lua_upvalueindex(1))) {
    return luaL_error(state, "invalid Lua preload chunk");
  }
  candidate = lua_touserdata(state, lua_upvalueindex(1));
  for (chunk = runtime->chunks; chunk != NULL; chunk = chunk->next) {
    if ((void *)chunk == candidate) {
      break;
    }
  }
  if (chunk == NULL) {
    return luaL_error(state, "invalid Lua preload chunk");
  }

  nargs = lua_gettop(state);
  result = luaL_loadbuffer(
      state, (const char *)chunk->source, chunk->source_size,
      chunk->chunk_name != NULL ? chunk->chunk_name : chunk->module_name);
  if (result != LUA_OK) {
    return lua_error(state);
  }

  lua_insert(state, 1);
  lua_call(state, nargs, 1);
  return 1;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
const char *cpkt_lua_runtime_lua_version(void) { return LUA_VERSION; }

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
const char *cpkt_lua_runtime_facade_version(void) { return "1"; }

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status cpkt_lua_runtime_new(cpkt_lua_runtime **out) {
  return cpkt_lua_runtime_new_with_limit(out, 0);
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status cpkt_lua_runtime_new_with_limit(cpkt_lua_runtime **out,
                                                        size_t max_bytes) {
  cpkt_lua_runtime_allocator_config config;

  memset(&config, 0, sizeof(config));
  config.max_bytes = max_bytes;
  return cpkt_lua_runtime_new_with_allocator(out, &config);
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status cpkt_lua_runtime_new_with_allocator(
    cpkt_lua_runtime **out,
    const cpkt_lua_runtime_allocator_config *allocator_config) {
  struct cpkt_lua_runtime_allocator allocator;
  cpkt_lua_runtime *runtime;
  lua_State *state;
  cpkt_lua_runtime_status status;

  if (out == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  *out = NULL;
  if (allocator_config != NULL && ((allocator_config->alloc_fn == NULL &&
                                    allocator_config->free_fn != NULL) ||
                                   (allocator_config->alloc_fn != NULL &&
                                    allocator_config->free_fn == NULL) ||
                                   (allocator_config->alloc_fn == NULL &&
                                    allocator_config->realloc_fn != NULL))) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }

  cpkt_lua_runtime_allocator_init(&allocator, allocator_config, 0);
  runtime = (cpkt_lua_runtime *)cpkt_lua_runtime_allocator_alloc(
      &allocator, sizeof(*runtime));
  if (runtime == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ALLOC;
  }
  memset(runtime, 0, sizeof(*runtime));
  runtime->allocator = allocator;

  state = lua_newstate(cpkt_lua_runtime_lua_alloc, &runtime->allocator, 0);
  if (state == NULL) {
    allocator = runtime->allocator;
    cpkt_lua_runtime_allocator_free(&allocator, runtime, sizeof(*runtime));
    return CPKT_LUA_RUNTIME_ERR_ALLOC;
  }

  runtime->state = state;
  lua_setwarnf(state, cpkt_lua_runtime_native_warning, runtime);
  status = cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_store_state_protected, runtime, 0,
      CPKT_LUA_RUNTIME_ERR_RUNTIME);
  if (status != CPKT_LUA_RUNTIME_OK) {
    lua_close(runtime->state);
    runtime->state = NULL;
    cpkt_lua_runtime_allocator_free(
        &runtime->allocator, runtime->last_error,
        runtime->last_error != NULL ? strlen(runtime->last_error) + 1 : 0);
    allocator = runtime->allocator;
    cpkt_lua_runtime_allocator_free(&allocator, runtime, sizeof(*runtime));
    return status;
  }
  *out = runtime;

  return CPKT_LUA_RUNTIME_OK;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
void cpkt_lua_runtime_free(cpkt_lua_runtime *runtime) {
  struct cpkt_lua_runtime_allocator allocator;
  struct cpkt_lua_runtime_c_module *module;
  struct cpkt_lua_runtime_c_module *next;

  if (runtime == NULL) {
    return;
  }
  allocator = runtime->allocator;

  if (runtime->state != NULL) {
    /* Destruction is a separate host invocation with the configured budget. */
    runtime->instruction_limit_hit = 0;
    runtime->instruction_remaining = runtime->instruction_limit;
    cpkt_lua_runtime_apply_instruction_hook(runtime, runtime->state);
    lua_close(runtime->state);
  }
  cpkt_lua_runtime_clear_chunks(runtime);
  module = runtime->c_modules;
  while (module != NULL) {
    next = module->next;
    cpkt_lua_runtime_allocator_free(&runtime->allocator, module,
                                    sizeof(*module));
    module = next;
  }
  cpkt_lua_runtime_allocator_free(
      &runtime->allocator, runtime->last_error,
      runtime->last_error != NULL ? strlen(runtime->last_error) + 1 : 0);
  cpkt_lua_runtime_allocator_free(&allocator, runtime, sizeof(*runtime));
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
void cpkt_lua_runtime_set_context(cpkt_lua_runtime *runtime, void *context) {
  if (runtime != NULL) {
    runtime->context = context;
  }
}

/** Implements the public Lua warning callback operation. */
void cpkt_lua_runtime_set_warning_callback(cpkt_lua_runtime *runtime,
                                           cpkt_lua_runtime_warning_fn callback,
                                           void *context) {
  if (runtime != NULL) {
    runtime->warning_callback = callback;
    runtime->warning_context = context;
  }
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
void *cpkt_lua_runtime_context(const cpkt_lua_runtime *runtime) {
  if (runtime == NULL) {
    return NULL;
  }
  return runtime->context;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
void *cpkt_lua_runtime_context_from_state(void *lua_state) {
  cpkt_lua_runtime *runtime;

  if (lua_state == NULL) {
    return NULL;
  }

  runtime = cpkt_lua_runtime_from_state((lua_State *)lua_state);
  if (runtime == NULL) {
    return NULL;
  }
  return runtime->context;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status cpkt_lua_runtime_openlibs(cpkt_lua_runtime *runtime) {
  if (runtime == NULL || runtime->state == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }

  return cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_ALL);
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status cpkt_lua_runtime_open_libs(cpkt_lua_runtime *runtime,
                                                   int libs) {
  cpkt_lua_runtime_status status;

  if (runtime == NULL || runtime->state == NULL || libs < 0) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }

  status = CPKT_LUA_RUNTIME_OK;
  if ((libs & CPKT_LUA_RUNTIME_LIB_BASE) != 0) {
    status = cpkt_lua_runtime_open_one_lib(runtime, "_G", luaopen_base);
    if (status == CPKT_LUA_RUNTIME_OK) {
      status = cpkt_lua_runtime_protected_call(
          runtime, cpkt_lua_runtime_wrap_protected_calls, runtime, 0,
          CPKT_LUA_RUNTIME_ERR_RUNTIME);
    }
  }
  if (status == CPKT_LUA_RUNTIME_OK &&
      (libs & CPKT_LUA_RUNTIME_LIB_PACKAGE) != 0) {
    status = cpkt_lua_runtime_open_one_lib(runtime, LUA_LOADLIBNAME,
                                           luaopen_package);
  }
  if (status == CPKT_LUA_RUNTIME_OK &&
      (libs & CPKT_LUA_RUNTIME_LIB_COROUTINE) != 0) {
    status = cpkt_lua_runtime_open_one_lib(runtime, LUA_COLIBNAME,
                                           luaopen_coroutine);
    if (status == CPKT_LUA_RUNTIME_OK) {
      status = cpkt_lua_runtime_wrap_coroutine_library_checked(runtime);
    }
  }
  if (status == CPKT_LUA_RUNTIME_OK &&
      (libs & CPKT_LUA_RUNTIME_LIB_TABLE) != 0) {
    status =
        cpkt_lua_runtime_open_one_lib(runtime, LUA_TABLIBNAME, luaopen_table);
  }
  if (status == CPKT_LUA_RUNTIME_OK && (libs & CPKT_LUA_RUNTIME_LIB_IO) != 0) {
    status = cpkt_lua_runtime_open_one_lib(runtime, LUA_IOLIBNAME, luaopen_io);
  }
  if (status == CPKT_LUA_RUNTIME_OK && (libs & CPKT_LUA_RUNTIME_LIB_OS) != 0) {
    status = cpkt_lua_runtime_open_one_lib(runtime, LUA_OSLIBNAME, luaopen_os);
  }
  if (status == CPKT_LUA_RUNTIME_OK &&
      (libs & CPKT_LUA_RUNTIME_LIB_STRING) != 0) {
    status =
        cpkt_lua_runtime_open_one_lib(runtime, LUA_STRLIBNAME, luaopen_string);
  }
  if (status == CPKT_LUA_RUNTIME_OK &&
      (libs & CPKT_LUA_RUNTIME_LIB_UTF8) != 0) {
    status =
        cpkt_lua_runtime_open_one_lib(runtime, LUA_UTF8LIBNAME, luaopen_utf8);
  }
  if (status == CPKT_LUA_RUNTIME_OK &&
      (libs & CPKT_LUA_RUNTIME_LIB_MATH) != 0) {
    status =
        cpkt_lua_runtime_open_one_lib(runtime, LUA_MATHLIBNAME, luaopen_math);
  }
  if (status == CPKT_LUA_RUNTIME_OK &&
      (libs & CPKT_LUA_RUNTIME_LIB_DEBUG) != 0) {
    status =
        cpkt_lua_runtime_open_one_lib(runtime, LUA_DBLIBNAME, luaopen_debug);
    if (status == CPKT_LUA_RUNTIME_OK) {
      status = cpkt_lua_runtime_disable_debug_sethook_checked(runtime);
    }
  }

  if (status == CPKT_LUA_RUNTIME_OK) {
    cpkt_lua_runtime_clear_error(runtime);
  }
  return status;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_traceback(cpkt_lua_runtime *runtime, int enabled) {
  if (runtime == NULL || runtime->state == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  runtime->traceback_enabled = enabled != 0;
  cpkt_lua_runtime_clear_error(runtime);
  return CPKT_LUA_RUNTIME_OK;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_instruction_limit(cpkt_lua_runtime *runtime,
                                       int instruction_count) {
  if (runtime == NULL || runtime->state == NULL || instruction_count <= 0) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  runtime->instruction_limit = instruction_count;
  cpkt_lua_runtime_apply_instruction_hook(runtime, runtime->state);
  cpkt_lua_runtime_clear_error(runtime);
  return CPKT_LUA_RUNTIME_OK;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status
cpkt_lua_runtime_clear_instruction_limit(cpkt_lua_runtime *runtime) {
  if (runtime == NULL || runtime->state == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  runtime->instruction_limit = 0;
  runtime->instruction_limit_hit = 0;
  cpkt_lua_runtime_apply_instruction_hook(runtime, runtime->state);
  cpkt_lua_runtime_clear_error(runtime);
  return CPKT_LUA_RUNTIME_OK;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_package_path(cpkt_lua_runtime *runtime, const char *path) {
  if (runtime == NULL || runtime->state == NULL || path == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  return cpkt_lua_runtime_set_package_field(runtime, "path", path);
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status
cpkt_lua_runtime_prepend_package_path(cpkt_lua_runtime *runtime,
                                      const char *path) {
  if (runtime == NULL || runtime->state == NULL || path == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  return cpkt_lua_runtime_prepend_package_field(runtime, "path", path);
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_package_cpath(cpkt_lua_runtime *runtime,
                                   const char *path) {
  if (runtime == NULL || runtime->state == NULL || path == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  return cpkt_lua_runtime_set_package_field(runtime, "cpath", path);
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status
cpkt_lua_runtime_prepend_package_cpath(cpkt_lua_runtime *runtime,
                                       const char *path) {
  if (runtime == NULL || runtime->state == NULL || path == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  return cpkt_lua_runtime_prepend_package_field(runtime, "cpath", path);
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_global_string(cpkt_lua_runtime *runtime, const char *name,
                                   const char *value) {
  struct cpkt_lua_runtime_global_string_context context;

  if (runtime == NULL || runtime->state == NULL || name == NULL ||
      value == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  context.name = name;
  context.value = value;
  return cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_set_global_string_protected, &context, 0,
      CPKT_LUA_RUNTIME_ERR_RUNTIME);
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_global_boolean(cpkt_lua_runtime *runtime, const char *name,
                                    int value) {
  struct cpkt_lua_runtime_global_boolean_context context;

  if (runtime == NULL || runtime->state == NULL || name == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  context.name = name;
  context.value = value;
  return cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_set_global_boolean_protected, &context, 0,
      CPKT_LUA_RUNTIME_ERR_RUNTIME);
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_global_number(cpkt_lua_runtime *runtime, const char *name,
                                   double value) {
  struct cpkt_lua_runtime_global_number_context context;

  if (runtime == NULL || runtime->state == NULL || name == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  context.name = name;
  context.value = value;
  return cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_set_global_number_protected, &context, 0,
      CPKT_LUA_RUNTIME_ERR_RUNTIME);
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_global_integer(cpkt_lua_runtime *runtime, const char *name,
                                    long value) {
  struct cpkt_lua_runtime_global_integer_context context;

  if (runtime == NULL || runtime->state == NULL || name == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  context.name = name;
  context.value = value;
  return cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_set_global_integer_protected, &context, 0,
      CPKT_LUA_RUNTIME_ERR_RUNTIME);
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status
cpkt_lua_runtime_register_c_module(cpkt_lua_runtime *runtime,
                                   const char *module_name,
                                   cpkt_lua_runtime_c_module_open_fn opener) {
  struct cpkt_lua_runtime_c_module_context context;
  struct cpkt_lua_runtime_c_module *module;
  cpkt_lua_runtime_status status;

  if (runtime == NULL || runtime->state == NULL || module_name == NULL ||
      opener == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }

  module = (struct cpkt_lua_runtime_c_module *)cpkt_lua_runtime_allocator_alloc(
      &runtime->allocator, sizeof(*module));
  if (module == NULL) {
    return cpkt_lua_runtime_set_error(runtime, CPKT_LUA_RUNTIME_ERR_ALLOC,
                                      "out of memory");
  }
  module->opener = opener;
  module->next = NULL;
  context.module_name = module_name;
  context.module = module;
  status = cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_register_c_module_protected, &context, 0,
      CPKT_LUA_RUNTIME_ERR_RUNTIME);
  if (status == CPKT_LUA_RUNTIME_OK) {
    module->next = runtime->c_modules;
    runtime->c_modules = module;
  } else {
    cpkt_lua_runtime_allocator_free(&runtime->allocator, module,
                                    sizeof(*module));
  }
  return status;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status cpkt_lua_runtime_register_lua_module(
    cpkt_lua_runtime *runtime, const char *module_name,
    const unsigned char *source, size_t source_size, const char *chunk_name) {
  cpkt_lua_runtime_status status;
  struct cpkt_lua_runtime_chunk *chunk;
  struct cpkt_lua_runtime_lua_module_context context;

  if (runtime == NULL || runtime->state == NULL || module_name == NULL ||
      source == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }

  chunk = (struct cpkt_lua_runtime_chunk *)cpkt_lua_runtime_allocator_alloc(
      &runtime->allocator, sizeof(*chunk));
  if (chunk == NULL) {
    return cpkt_lua_runtime_set_error(runtime, CPKT_LUA_RUNTIME_ERR_ALLOC,
                                      "out of memory");
  }
  memset(chunk, 0, sizeof(*chunk));

  chunk->module_name =
      cpkt_lua_runtime_strdup(runtime, module_name, &chunk->module_name_size);
  chunk->chunk_name = cpkt_lua_runtime_strdup(
      runtime, chunk_name != NULL ? chunk_name : module_name,
      &chunk->chunk_name_size);
  chunk->source_alloc_size = source_size != 0 ? source_size : 1;
  chunk->source = (unsigned char *)cpkt_lua_runtime_allocator_alloc(
      &runtime->allocator, chunk->source_alloc_size);
  if (chunk->module_name == NULL || chunk->chunk_name == NULL ||
      chunk->source == NULL) {
    cpkt_lua_runtime_allocator_free(&runtime->allocator, chunk->module_name,
                                    chunk->module_name_size);
    cpkt_lua_runtime_allocator_free(&runtime->allocator, chunk->chunk_name,
                                    chunk->chunk_name_size);
    cpkt_lua_runtime_allocator_free(&runtime->allocator, chunk->source,
                                    chunk->source_alloc_size);
    cpkt_lua_runtime_allocator_free(&runtime->allocator, chunk, sizeof(*chunk));
    return cpkt_lua_runtime_set_error(runtime, CPKT_LUA_RUNTIME_ERR_ALLOC,
                                      "out of memory");
  }
  if (source_size != 0) {
    memcpy(chunk->source, source, source_size);
  }
  chunk->source_size = source_size;

  context.chunk = chunk;
  status = cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_register_lua_module_protected, &context, 0,
      CPKT_LUA_RUNTIME_ERR_RUNTIME);
  if (status != CPKT_LUA_RUNTIME_OK) {
    cpkt_lua_runtime_free_chunk(runtime, chunk);
    return status;
  }

  chunk->next = runtime->chunks;
  runtime->chunks = chunk;

  cpkt_lua_runtime_clear_error(runtime);
  return CPKT_LUA_RUNTIME_OK;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status cpkt_lua_runtime_require(cpkt_lua_runtime *runtime,
                                                 const char *module_name) {
  struct cpkt_lua_runtime_require_context context;
  cpkt_lua_runtime_status status;

  if (runtime == NULL || runtime->state == NULL || module_name == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }

  context.module_name = module_name;
  status = cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_require_protected, &context, 1,
      CPKT_LUA_RUNTIME_ERR_RUNTIME);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return status;
  }
  lua_pop(runtime->state, 1);
  cpkt_lua_runtime_clear_error(runtime);
  return CPKT_LUA_RUNTIME_OK;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status cpkt_lua_runtime_run_file(cpkt_lua_runtime *runtime,
                                                  const char *path, int argc,
                                                  const char *const *argv,
                                                  int flags) {
  cpkt_lua_runtime_status status;
  struct cpkt_lua_runtime_load_file_context context;

  if (runtime == NULL || runtime->state == NULL || path == NULL || argc < 0) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  if (argc > 0 && argv == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }

  status = cpkt_lua_runtime_maybe_openlibs(runtime, flags);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return status;
  }

  context.args.script_name = path;
  context.args.argc = argc;
  context.args.argv = argv;
  context.path = path;
  status = cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_load_file_protected, &context, 1,
      CPKT_LUA_RUNTIME_ERR_LOAD);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return status;
  }

  status = cpkt_lua_runtime_call(runtime, 0, LUA_MULTRET);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return status;
  }

  lua_settop(runtime->state, 0);
  cpkt_lua_runtime_clear_error(runtime);
  return CPKT_LUA_RUNTIME_OK;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
cpkt_lua_runtime_status cpkt_lua_runtime_run_buffer(
    cpkt_lua_runtime *runtime, const unsigned char *source, size_t source_size,
    const char *chunk_name, int argc, const char *const *argv, int flags) {
  cpkt_lua_runtime_status status;
  struct cpkt_lua_runtime_load_buffer_context context;

  if (runtime == NULL || runtime->state == NULL || source == NULL || argc < 0) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }
  if (argc > 0 && argv == NULL) {
    return CPKT_LUA_RUNTIME_ERR_ARG;
  }

  status = cpkt_lua_runtime_maybe_openlibs(runtime, flags);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return status;
  }

  context.args.script_name = chunk_name;
  context.args.argc = argc;
  context.args.argv = argv;
  context.source = source;
  context.source_size = source_size;
  context.chunk_name = chunk_name;
  status = cpkt_lua_runtime_protected_call(
      runtime, cpkt_lua_runtime_load_buffer_protected, &context, 1,
      CPKT_LUA_RUNTIME_ERR_LOAD);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return status;
  }

  status = cpkt_lua_runtime_call(runtime, 0, LUA_MULTRET);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return status;
  }

  lua_settop(runtime->state, 0);
  cpkt_lua_runtime_clear_error(runtime);
  return CPKT_LUA_RUNTIME_OK;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
const char *cpkt_lua_runtime_error(const cpkt_lua_runtime *runtime) {
  if (runtime == NULL || runtime->last_error == NULL) {
    return "";
  }
  return runtime->last_error;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
void cpkt_lua_runtime_clear_error(cpkt_lua_runtime *runtime) {
  if (runtime == NULL) {
    return;
  }
  cpkt_lua_runtime_allocator_free(
      &runtime->allocator, runtime->last_error,
      runtime->last_error != NULL ? strlen(runtime->last_error) + 1 : 0);
  runtime->last_error = NULL;
}

/** Implements the public Lua runtime facade function declared in
 * <cpkt/lua_runtime.h>. */
const char *cpkt_lua_runtime_status_string(cpkt_lua_runtime_status status) {
  switch (status) {
  case CPKT_LUA_RUNTIME_OK:
    return "ok";
  case CPKT_LUA_RUNTIME_ERR_ARG:
    return "invalid argument";
  case CPKT_LUA_RUNTIME_ERR_ALLOC:
    return "allocation failure";
  case CPKT_LUA_RUNTIME_ERR_LOAD:
    return "load error";
  case CPKT_LUA_RUNTIME_ERR_RUNTIME:
    return "runtime error";
  case CPKT_LUA_RUNTIME_ERR_LIMIT:
    return "limit exceeded";
  }

  return "unknown status";
}
