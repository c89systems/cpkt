#include <cpkt/lua_runtime.h>
#include <stdio.h>
#include <string.h>

struct runtime_case {
  const char *name;
  const char *source;
  int limited;
  cpkt_lua_runtime_status expected;
};

static const struct runtime_case cases[] = {
    {"large_arguments",
     "local f=coroutine.wrap(function(...) return select('#',...) end); "
     "assert(f(table.unpack({},1,1000))==1000)",
     0, CPKT_LUA_RUNTIME_OK},
    {"large_results",
     "local f=coroutine.wrap(function() return table.unpack({},1,1000) end); "
     "assert(select('#',f())==1000)",
     0, CPKT_LUA_RUNTIME_OK},
    {"large_yields",
     "local f=coroutine.wrap(function() "
     "coroutine.yield(table.unpack({},1,1000)); return 17 end); "
     "assert(select('#',f())==1000); assert(f()==17)",
     0, CPKT_LUA_RUNTIME_OK},
    {"closed_error",
     "local closed=false; local f=coroutine.wrap(function() "
     "local resource <close> = setmetatable({}, {__close=function() "
     "closed=true end}); error('original failure') end); "
     "local ok,err=pcall(f); assert(not ok); assert(closed); "
     "assert(tostring(err):match('original failure'))",
     0, CPKT_LUA_RUNTIME_OK},
    {"close_error_replaced",
     "local f=coroutine.wrap(function() "
     "local resource <close> = setmetatable({}, {__close=function() "
     "error('close failure') end}); error('original failure') end); "
     "local ok,err=pcall(f); assert(not ok); "
     "assert(tostring(err):match('close failure'))",
     0, CPKT_LUA_RUNTIME_OK},
    {"protected_limit",
     "while true do pcall(function() while true do end end) end", 1,
     CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"xprotected_limit",
     "while true do xpcall(function() while true do end end, "
     "function(err) return 'caught' end) end",
     1, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"registry_limit",
     "for k in pairs(debug.getregistry()) do "
     "if type(k)=='userdata' then debug.getregistry()[k]=nil end end; "
     "while true do end",
     1, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"protected_yields",
     "local f=coroutine.wrap(function() "
     "local ok,v=pcall(function() return coroutine.yield(91) end); "
     "assert(ok and v==92); "
     "local ok,w=xpcall(function() return coroutine.yield(93) end, tostring); "
     "assert(ok and w==94); return 95 end); "
     "assert(f()==91); assert(f(92)==93); assert(f(94)==95)",
     0, CPKT_LUA_RUNTIME_OK},
    {"yielded_protected_limit",
     "local f=coroutine.wrap(function() pcall(function() "
     "coroutine.yield('ready'); while true do end end) end); "
     "assert(f()=='ready'); while true do pcall(f) end",
     1, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"upvalue_limit",
     "debug.setupvalue(coroutine.create,2,io.stdout); "
     "debug.setupvalue(coroutine.resume,2,io.stdout); "
     "local co=coroutine.create(function() while true do end end); "
     "coroutine.resume(co)",
     1, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"existing_wrap_limit",
     "local f=coroutine.wrap(function() coroutine.yield('ready'); "
     "while true do end end); _G.policy_saved_wrap=f; assert(f()=='ready')",
     0, CPKT_LUA_RUNTIME_OK}};

static int exercise(const struct runtime_case *item) {
  cpkt_lua_runtime *runtime;
  cpkt_lua_runtime_status status;
  const char *message;
  int failed;
  const char *recovery;

  runtime = NULL;
  failed = 0;
  status = cpkt_lua_runtime_new(&runtime);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return 1;
  }
  status = cpkt_lua_runtime_openlibs(runtime);
  if (status == CPKT_LUA_RUNTIME_OK && item->limited) {
    status = cpkt_lua_runtime_set_instruction_limit(runtime, 1000);
  }
  if (status == CPKT_LUA_RUNTIME_OK) {
    status = cpkt_lua_runtime_run_buffer(
        runtime, (const unsigned char *)item->source, strlen(item->source),
        item->name, 0, NULL, 0);
  }
  if (status != item->expected) {
    message = cpkt_lua_runtime_error(runtime);
    fprintf(stderr, "%s: expected %d, got %d (%s)\n", item->name,
            (int)item->expected, (int)status,
            message != NULL ? message : "no diagnostic");
    failed = 1;
  }
  if (!failed && strcmp(item->name, "existing_wrap_limit") == 0) {
    recovery = "policy_saved_wrap()";
    status = cpkt_lua_runtime_set_instruction_limit(runtime, 1000);
    if (status == CPKT_LUA_RUNTIME_OK) {
      status = cpkt_lua_runtime_run_buffer(
          runtime, (const unsigned char *)recovery, strlen(recovery),
          "existing-wrap-limit", 0, NULL, 0);
    }
    if (status != CPKT_LUA_RUNTIME_ERR_LIMIT) {
      fprintf(stderr, "an existing wrapper ignored the changed limit\n");
      failed = 1;
    }
  }
  if (!failed && item->limited) {
    recovery = "local total=0; for i=1,20 do total=total+i end; "
               "assert(total==210); assert(pcall(function() return true end))";
    status = cpkt_lua_runtime_run_buffer(
        runtime, (const unsigned char *)recovery, strlen(recovery),
        "fresh-budget", 0, NULL, 0);
    if (status != CPKT_LUA_RUNTIME_OK) {
      fprintf(stderr, "%s: the next host invocation did not reset its budget\n",
              item->name);
      failed = 1;
    }
    if (!failed) {
      recovery = "while true do pcall(function() while true do end end) end";
      status = cpkt_lua_runtime_run_buffer(
          runtime, (const unsigned char *)recovery, strlen(recovery),
          "repeated-limit", 0, NULL, 0);
      if (status != CPKT_LUA_RUNTIME_ERR_LIMIT) {
        fprintf(stderr, "%s: a subsequent invocation bypassed the limit\n",
                item->name);
        failed = 1;
      }
    }
  }
  if (!failed) {
    recovery = "assert(pcall(function() return true end)); "
               "assert(xpcall(function() return true end,tostring)); "
               "assert(coroutine.wrap(function() return 7 end)()==7)";
    status = cpkt_lua_runtime_clear_instruction_limit(runtime);
    if (status == CPKT_LUA_RUNTIME_OK) {
      status =
          cpkt_lua_runtime_run_buffer(runtime, (const unsigned char *)recovery,
                                      strlen(recovery), "recovery", 0, NULL, 0);
    }
    if (status != CPKT_LUA_RUNTIME_OK) {
      fprintf(stderr, "%s: runtime failed recovery after clearing the limit\n",
              item->name);
      failed = 1;
    }
  }
  cpkt_lua_runtime_free(runtime);
  return failed;
}

int main(int argc, char **argv) {
  size_t index;
  if (argc != 2) {
    return 2;
  }
  for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
    if (strcmp(argv[1], cases[index].name) == 0) {
      return exercise(&cases[index]);
    }
  }
  fprintf(stderr, "unknown runtime case: %s\n", argv[1]);
  return 2;
}
