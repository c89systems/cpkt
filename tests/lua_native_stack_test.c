#define _GNU_SOURCE
#include <cpkt/lua_runtime.h>
#include <errno.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct stack_case {
  const char *name;
  const char *program;
};

static const struct stack_case cases[] = {
    {"pcall", "local function f() local ok,v=pcall(f); "
              "return ok and v or tostring(v) end; "
              "assert(f():find('stack overflow',1,true))"},
    {"xpcall",
     "local handlers=0; local function handler(e) "
     "handlers=handlers+1; return debug.traceback(e) end; "
     "local function f() local ok,v=xpcall(f,handler); "
     "return ok and v or tostring(v) end; "
     "local message=f(); assert(message:find('stack overflow',1,true)); "
     "assert(message:find('stack traceback',1,true)); "
     "assert(handlers>0)"},
    {"handler",
     "local function handler(e) return "
     "string.gsub('x','x',function() return handler(e) end) end; "
     "local ok,e=xpcall(function() error('original') end,handler); "
     "assert(not ok and tostring(e):find('error in error handling',1,true))"},
    {"callback", "local function f() return string.gsub('x','x',f) end; "
                 "local ok,e=pcall(f); assert(not ok); "
                 "assert(tostring(e):find('stack overflow',1,true))"},
    {"module", "setmetatable(package.preload,{__index=function(t,n) "
               "return function() return require(n..'x') end end}); "
               "local ok,e=pcall(require,'stack_module'); assert(not ok); "
               "assert(tostring(e):find('stack overflow',1,true))"},
    {"parser", "local f,e=load('return '..string.rep('(',1000)..'1'.."
               "string.rep(')',1000)); assert(f==nil and type(e)=='string'); "
               "assert(e:find('stack overflow',1,true) or "
               "e:find('too many C levels',1,true))"},
    {"resume",
     "local function f() local co=coroutine.create(f); "
     "local ok,v=coroutine.resume(co); return ok and v or tostring(v) end; "
     "assert(f():find('stack overflow',1,true))"},
    {"close",
     "local closed=0; local mt={__close=function() closed=closed+1 end}; "
     "local function f() local value <close> =setmetatable({},mt); "
     "return string.gsub('x','x',f) end; "
     "local ok,e=pcall(f); assert(not ok and closed>0); "
     "assert(tostring(e):find('stack overflow',1,true))"},
    {"native", "local ok,e=pcall(native_recurse); assert(not ok); "
               "assert(tostring(e):find('stack overflow',1,true))"},
    {"reader", "local ok,e=pcall(native_reader); assert(not ok); "
               "assert(tostring(e):find('stack overflow',1,true))"},
    {"reserve", "local child=coroutine.create(function() return true end); "
                "local function handler(e) assert(coroutine.resume(child)); "
                "return e end; "
                "local function f() return string.gsub('x','x',f) end; "
                "local ok,e=xpcall(f,handler); assert(not ok); "
                "assert(native_check_child(child))"},
    {"migration", ""},
    {"yield_entry", ""},
    {"self_close", ""},
#ifdef CPKT_TEST_WRAP_BOUNDS
    {"bounds", ""},
#endif
};

#ifdef CPKT_TEST_WRAP_BOUNDS
/* Only the static regression binary wraps discovery; production has no switch.
 */
static int fail_bounds;
static unsigned bounds_handler_calls;
int __real_pthread_getattr_np(pthread_t thread, pthread_attr_t *attributes);
int __wrap_pthread_getattr_np(pthread_t thread, pthread_attr_t *attributes);
int __wrap_pthread_getattr_np(pthread_t thread, pthread_attr_t *attributes) {
  return fail_bounds ? EACCES : __real_pthread_getattr_np(thread, attributes);
}

static int bounds_handler(lua_State *state) {
  (void)state;
  ++bounds_handler_calls;
  return 1;
}

static int bounds_value(lua_State *state) {
  lua_pushinteger(state, 42);
  return 1;
}

static int check_bounds(lua_State *state) {
  const char *message;
  int status, failed;
  bounds_handler_calls = 0;
  lua_pushcfunction(state, bounds_handler);
  lua_pushcfunction(state, bounds_value);
  fail_bounds = 1;
  status = lua_pcall(state, 0, 1, 1);
  fail_bounds = 0;
  message = lua_tostring(state, -1);
  failed = status != LUA_ERRRUN || bounds_handler_calls != 0 ||
           message == NULL ||
           strstr(message, "native stack bounds unavailable") == NULL;
  lua_settop(state, 0);
  lua_pushcfunction(state, bounds_value);
  status = lua_pcall(state, 0, 1, 0);
  failed |= status != LUA_OK || lua_tointeger(state, -1) != 42;
  lua_settop(state, 0);
  return failed;
}
#endif

struct trace_context {
  cpkt_lua_runtime *runtime;
  unsigned calls;
  int failed;
};

struct worker_context {
  const struct stack_case *test;
  lua_State *migrating_state;
  int mode;
  int headroom;
  int failed;
};

/* Put a yielded coroutine's next resume inside the normal overflow boundary.
 * These bounded frames are deliberate caller stack consumption, not Lua work.
 */
__attribute__((noinline)) static int
resume_below_limit(lua_State *child, lua_State *from, uintptr_t low) {
  volatile unsigned char caller[1024];
  int results, status;
  caller[0] = 1;
  caller[sizeof(caller) - 1] = 2;
  if ((uintptr_t)&caller[0] > low + 15 * 1024)
    status = resume_below_limit(child, from, low);
  else
    status = lua_resume(child, from, 0, &results);
  return caller[0] == 1 && caller[sizeof(caller) - 1] == 2 ? status : -1;
}

static int check_yield_entry(lua_State *root) {
  lua_State *child = lua_newthread(root);
  uintptr_t low;
  int status, results, failed;
#if defined(__linux__)
  pthread_attr_t attributes;
  void *base;
  size_t size;
  if (pthread_getattr_np(pthread_self(), &attributes) != 0)
    return 1;
  status = pthread_attr_getstack(&attributes, &base, &size);
  pthread_attr_destroy(&attributes);
  if (status != 0)
    return 1;
  low = (uintptr_t)base;
#elif defined(__APPLE__)
  low = (uintptr_t)pthread_get_stackaddr_np(pthread_self()) -
        pthread_get_stacksize_np(pthread_self());
#else
#error Unsupported native-stack regression platform
#endif
  if (luaL_loadstring(
          child, "local ok,e=pcall(coroutine.yield); return ok,e") != LUA_OK ||
      lua_resume(child, root, 0, &results) != LUA_YIELD)
    return 1;
  status = resume_below_limit(child, root, low);
  failed = status != LUA_OK || lua_status(child) != LUA_OK ||
           lua_toboolean(child, 1) || lua_type(child, 2) != LUA_TSTRING ||
           strstr(lua_tostring(child, 2), "stack overflow") == NULL;
  if (!failed)
    failed = lua_resume(child, root, 0, &results) != LUA_ERRRUN;
  lua_pop(root, 1);
  return failed;
}

static int native_self_close(lua_State *state) {
  return lua_closethread(state, state);
}

static int check_self_close(lua_State *root) {
  lua_State *child;
  int results, status;
  lua_pushcfunction(root, native_self_close);
  lua_setglobal(root, "native_self_close");
  child = lua_newthread(root);
  status = luaL_loadstring(
      child, "local function f() return string.gsub('x','x',f) end; "
             "xpcall(f,native_self_close)");
  if (status == LUA_OK)
    status = lua_resume(child, root, 0, &results);
  lua_pop(root, 1);
  if (status != LUA_OK)
    return 1;
  return luaL_dostring(root, cases[0].program) != LUA_OK;
}

static int native_recurse(lua_State *state) {
  lua_pushcfunction(state, native_recurse);
  lua_call(state, 0, 0);
  return 0;
}

static const char *nested_reader(lua_State *state, void *context,
                                 size_t *size) {
  int status;
  (void)context;
  *size = 0;
  status = lua_load(state, nested_reader, NULL, "nested-native-reader", "t");
  if (status != LUA_OK)
    (void)lua_error(state);
  lua_pop(state, 1);
  return NULL;
}

static int native_reader(lua_State *state) {
  if (lua_load(state, nested_reader, NULL, "native-reader", "t") != LUA_OK)
    return lua_error(state);
  lua_pop(state, 1);
  return 0;
}

static int native_check_child(lua_State *state) {
  lua_State *child = lua_tothread(state, 1);
  const char *message;
  int ok;
  if (child == NULL || lua_resetthread(child) != LUA_OK)
    return luaL_error(state, "cannot reset test coroutine");
  ok = luaL_loadstring(
           child, "local function f() local ok,v=pcall(f); "
                  "return ok and v or tostring(v) end; return f()") == LUA_OK;
  if (ok)
    ok = lua_pcall(child, 0, 1, 0) == LUA_OK;
  message = lua_tostring(child, -1);
  lua_pushboolean(state, ok && message != NULL &&
                             strstr(message, "stack overflow") != NULL);
  return 1;
}

static int native_module(void *state) {
  lua_pushcfunction(state, native_recurse);
  lua_setglobal(state, "native_recurse");
  lua_pushcfunction(state, native_reader);
  lua_setglobal(state, "native_reader");
  lua_pushcfunction(state, native_check_child);
  lua_setglobal(state, "native_check_child");
  return 0;
}

static void trace(void *context, const cpkt_lua_runtime_trace_event *event) {
  struct trace_context *capture = context;
  (void)event;
  ++capture->calls;
  if (cpkt_lua_runtime_run_buffer(
          capture->runtime, (const unsigned char *)"return true", 11,
          "trace-reentry", 0, NULL, 0) != CPKT_LUA_RUNTIME_ERR_ARG)
    capture->failed = 1;
}

static int run_case(struct worker_context *context) {
  static const char recovery[] =
      "assert(pcall(function() return true end)); "
      "assert(xpcall(function() return true end,tostring)); "
      "assert(6*7==42)";
  const char *program = context->test->program;
  int status;
  if (strcmp(context->test->name, "yield_entry") == 0)
    return check_yield_entry(context->migrating_state);
  if (strcmp(context->test->name, "self_close") == 0)
    return check_self_close(context->migrating_state);
#ifdef CPKT_TEST_WRAP_BOUNDS
  if (strcmp(context->test->name, "bounds") == 0)
    return check_bounds(context->migrating_state);
#endif
  if (strcmp(context->test->name, "migration") == 0) {
    lua_State *state = context->migrating_state;
    int finalized;
    (void)lua_gc(state, LUA_GCRESTART);
    (void)lua_gc(state, LUA_GCCOLLECT);
    lua_getglobal(state, "native_stack_finalized");
    finalized = lua_toboolean(state, -1);
    lua_pop(state, 1);
    return !finalized;
  }
  if (context->mode < 2) {
    lua_State *root = context->migrating_state;
    lua_State *state = root;
    int results = 0;
    lua_pushcfunction(root, native_recurse);
    lua_setglobal(root, "native_recurse");
    lua_pushcfunction(root, native_reader);
    lua_setglobal(root, "native_reader");
    lua_pushcfunction(root, native_check_child);
    lua_setglobal(root, "native_check_child");
    if (context->mode == 1)
      state = lua_newthread(root);
    status = luaL_loadstring(state, program);
    if (status == LUA_OK)
      status = context->mode == 1 ? lua_resume(state, NULL, 0, &results)
                                  : lua_pcall(state, 0, 0, 0);
    if (status != LUA_OK) {
      fprintf(stderr, "Lua error: %s\n", lua_tostring(state, -1));
      return 1;
    }
    lua_settop(root, 0);
    status = luaL_loadstring(root, recovery);
    if (status == LUA_OK)
      status = lua_pcall(root, 0, 0, 0);
    return status != LUA_OK;
  } else {
    cpkt_lua_runtime *runtime = NULL;
    struct trace_context capture;
    if (cpkt_lua_runtime_new(&runtime) != CPKT_LUA_RUNTIME_OK)
      return 1;
    memset(&capture, 0, sizeof(capture));
    capture.runtime = runtime;
    if (cpkt_lua_runtime_openlibs(runtime) != CPKT_LUA_RUNTIME_OK ||
        cpkt_lua_runtime_register_c_module(
            runtime, "native_stack", native_module) != CPKT_LUA_RUNTIME_OK ||
        cpkt_lua_runtime_require(runtime, "native_stack") !=
            CPKT_LUA_RUNTIME_OK)
      return 1;
    if (context->mode == 3 &&
        cpkt_lua_runtime_set_trace_callback(runtime, trace, &capture) !=
            CPKT_LUA_RUNTIME_OK)
      return 1;
    status = cpkt_lua_runtime_run_buffer(
        runtime, (const unsigned char *)program, strlen(program),
        "native-stack", 0, NULL, 0);
    if (status != CPKT_LUA_RUNTIME_OK) {
      fprintf(stderr, "Runtime error: %s\n", cpkt_lua_runtime_error(runtime));
      cpkt_lua_runtime_free(runtime);
      return 1;
    }
    status = cpkt_lua_runtime_run_buffer(
        runtime, (const unsigned char *)recovery, sizeof(recovery) - 1,
        "stack-recovery", 0, NULL, 0);
    cpkt_lua_runtime_free(runtime);
    return status != CPKT_LUA_RUNTIME_OK || capture.failed ||
           (context->mode == 3 && capture.calls == 0);
  }
}

__attribute__((noinline)) static int
with_headroom(struct worker_context *context) {
  volatile unsigned char caller[32768];
  size_t i;
  int result;
  for (i = 0; i < sizeof(caller); ++i)
    caller[i] = (unsigned char)i;
  result = run_case(context);
  if (caller[0] != 0 || caller[sizeof(caller) - 1] != 255)
    return 1;
  return result;
}

static void *worker(void *argument) {
  struct worker_context *context = argument;
  context->failed =
      context->headroom ? with_headroom(context) : run_case(context);
  return NULL;
}

static int exercise(const struct stack_case *test) {
  struct worker_context context;
  pthread_attr_t attributes;
  const pthread_attr_t *selected_attributes = NULL;
  pthread_t thread;
  int mode, headroom;
  int migration = strcmp(test->name, "migration") == 0;
  int single_mode = test->program[0] == '\0';
  memset(&context, 0, sizeof(context));
  context.test = test;
#ifdef CPKT_TEST_SMALL_NATIVE_STACK
  /* glibc otherwise hides the default-musl-stack regression with several MiB.
   */
  if (pthread_attr_init(&attributes) != 0 ||
      pthread_attr_setstacksize(&attributes, 128 * 1024) != 0)
    return 2;
  selected_attributes = &attributes;
#else
  (void)attributes;
  /* musl and Darwin use unmodified pthread defaults. */
#endif
  context.migrating_state = luaL_newstate();
  if (context.migrating_state == NULL)
    return 2;
  luaL_openlibs(context.migrating_state);
  for (mode = 0; mode < (single_mode ? 1 : 4); ++mode) {
    for (headroom = 0; headroom < 2; ++headroom) {
      context.mode = mode;
      context.headroom = headroom;
      if (migration) {
        /* Collection must be the worker's first Lua operation. */
        (void)lua_gc(context.migrating_state, LUA_GCSTOP);
        if (luaL_dostring(context.migrating_state,
                          "native_stack_finalized=false; "
                          "do local value=setmetatable({}, {__gc=function() "
                          "native_stack_finalized=true end}) end") != LUA_OK)
          return 1;
      }
      if (pthread_create(&thread, selected_attributes, worker, &context) != 0 ||
          pthread_join(thread, NULL) != 0)
        return 2;
      if (context.failed) {
        fprintf(stderr, "%s: mode=%d caller=%d KiB\n", test->name, mode,
                headroom * 32);
        return 1;
      }
    }
  }
  lua_close(context.migrating_state);
#ifdef CPKT_TEST_SMALL_NATIVE_STACK
  pthread_attr_destroy(&attributes);
#endif
  return 0;
}

int main(int argc, char **argv) {
  size_t i;
  int found = 0;
  if (argc > 2)
    return 2;
  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    if (argc == 1 || strcmp(argv[1], cases[i].name) == 0) {
      found = 1;
      if (exercise(&cases[i]) != 0)
        return 1;
    }
  }
  return found ? 0 : 2;
}
