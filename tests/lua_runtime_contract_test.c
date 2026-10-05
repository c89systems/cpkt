#include <cpkt/lua.h>
#include <cpkt/lua_runtime.h>
#include <stdio.h>
#include <stdlib.h>
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
     0, CPKT_LUA_RUNTIME_OK},
    {"native_upvalues",
     "assert(debug.getupvalue(coroutine.create,1)==nil); "
     "assert(debug.getupvalue(coroutine.resume,1)==nil); "
     "assert(debug.getupvalue(coroutine.wrap,1)==nil); "
     "local f=coroutine.wrap(function() return 7 end); "
     "local _,co=debug.getupvalue(f,1); assert(type(co)=='thread'); "
     "assert(debug.getupvalue(f,2)==nil); "
     "policy_saved_resume=coroutine.resume; "
     "policy_saved_thread=coroutine.create(function() while true do end end)",
     0, CPKT_LUA_RUNTIME_OK},
    {"shared_resume_budget",
     "local co=coroutine.create(function() for j=1,10 do local sum=0; "
     "for i=1,200 do sum=sum+i end; coroutine.yield(sum) end end); "
     "for j=1,10 do assert(coroutine.resume(co)) end",
     1, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"shared_wrap_budget",
     "local f=coroutine.wrap(function() for j=1,10 do local sum=0; "
     "for i=1,200 do sum=sum+i end; coroutine.yield(sum) end end); "
     "for j=1,10 do assert(f()) end",
     1, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"error_conversion_oom", "", 0, CPKT_LUA_RUNTIME_ERR_ALLOC},
    {"numeric_error", "error(1729)", 0, CPKT_LUA_RUNTIME_ERR_RUNTIME},
    {"preload_registration", "", 0, CPKT_LUA_RUNTIME_OK},
    {"preload_arguments", "", 0, CPKT_LUA_RUNTIME_OK}};

struct failure_allocator {
  int reject_growth;
};

static void *allocate(void *user, size_t size) {
  struct failure_allocator *allocator;
  allocator = (struct failure_allocator *)user;
  return allocator->reject_growth ? NULL : malloc(size);
}

static void *resize(void *user, void *pointer, size_t old_size,
                    size_t new_size) {
  struct failure_allocator *allocator;
  allocator = (struct failure_allocator *)user;
  return allocator->reject_growth && new_size > old_size
             ? NULL
             : realloc(pointer, new_size);
}

static void release(void *user, void *pointer, size_t size) {
  (void)user;
  (void)size;
  free(pointer);
}

static int failing_numeric_module(void *state) {
  struct failure_allocator *allocator;
  cpkt_lua_integer value;
  allocator =
      (struct failure_allocator *)cpkt_lua_runtime_context_from_state(state);
  value.high = 0;
  value.low = 1729;
  cpkt_lua_pushinteger((cpkt_lua_state *)state, value);
  allocator->reject_growth = 1;
  return cpkt_lua_error((cpkt_lua_state *)state);
}

static int preload_c_module(void *state) {
  cpkt_lua_pushboolean((cpkt_lua_state *)state, 1);
  return 1;
}

static cpkt_lua_runtime_status run_text(cpkt_lua_runtime *runtime,
                                        const char *text) {
  return cpkt_lua_runtime_run_buffer(runtime, (const unsigned char *)text,
                                     strlen(text), "preload-contract", 0, NULL,
                                     0);
}

static cpkt_lua_runtime_status exercise_preload(cpkt_lua_runtime *runtime,
                                                int registration_case) {
  cpkt_lua_runtime_status status;
  const char *source;

  if (registration_case) {
    status = run_text(
        runtime, "preload_calls=0; setmetatable(package.preload, "
                 "{__newindex=function(t,k,v) preload_calls=preload_calls+1; "
                 "rawset(t,k,v); error('published loader then raised') end})");
    if (status != CPKT_LUA_RUNTIME_OK) {
      return status;
    }
  }
  source =
      "local name,data=...; return {name=name,data=data,count=select('#',...)}";
  status = cpkt_lua_runtime_register_lua_module(
      runtime, "preload_contract", (const unsigned char *)source,
      strlen(source), "preload-contract-module");
  if (status != CPKT_LUA_RUNTIME_OK) {
    return status;
  }
  if (registration_case) {
    status = cpkt_lua_runtime_register_c_module(runtime, "preload_c_contract",
                                                preload_c_module);
    if (status != CPKT_LUA_RUNTIME_OK) {
      return status;
    }
    status = run_text(runtime, "assert(preload_calls==0); "
                               "assert(require('preload_contract').count==2); "
                               "assert(require('preload_c_contract')==true); "
                               "assert(preload_calls==0)");
    if (status != CPKT_LUA_RUNTIME_OK) {
      return status;
    }
    source = "return {replacement=true}";
    status = cpkt_lua_runtime_register_lua_module(
        runtime, "preload_contract", (const unsigned char *)source,
        strlen(source), "preload-contract-replacement");
    if (status != CPKT_LUA_RUNTIME_OK) {
      return status;
    }
    return run_text(runtime,
                    "package.loaded.preload_contract=nil; "
                    "assert(require('preload_contract').replacement); "
                    "collectgarbage('collect'); assert(preload_calls==0)");
  }
  return run_text(
      runtime,
      "local value,data=require('preload_contract'); "
      "assert(value.name=='preload_contract'); "
      "assert(value.data==':preload:' and data==':preload:'); "
      "assert(value.count==2); "
      "assert(require('preload_contract')==value); "
      "local direct=package.preload.preload_contract('direct',17,nil); "
      "assert(direct.name=='direct' and direct.data==17); "
      "assert(direct.count==3); "
      "assert(package.preload.preload_contract().count==0)");
}

static int exercise(const struct runtime_case *item) {
  cpkt_lua_runtime *runtime;
  cpkt_lua_runtime_status status;
  const char *message;
  int failed;
  const char *recovery;
  struct failure_allocator allocator;
  cpkt_lua_runtime_allocator_config config;
  int conversion_case;

  runtime = NULL;
  failed = 0;
  conversion_case = strcmp(item->name, "error_conversion_oom") == 0;
  allocator.reject_growth = 0;
  memset(&config, 0, sizeof(config));
  config.user = &allocator;
  config.alloc_fn = allocate;
  config.realloc_fn = resize;
  config.free_fn = release;
  status = conversion_case
               ? cpkt_lua_runtime_new_with_allocator(&runtime, &config)
               : cpkt_lua_runtime_new(&runtime);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return 1;
  }
  status = cpkt_lua_runtime_openlibs(runtime);
  if (status == CPKT_LUA_RUNTIME_OK && item->limited) {
    status = cpkt_lua_runtime_set_instruction_limit(runtime, 1000);
  }
  if (status == CPKT_LUA_RUNTIME_OK && conversion_case) {
    cpkt_lua_runtime_set_context(runtime, &allocator);
    status = cpkt_lua_runtime_register_c_module(runtime, "numeric_failure",
                                                failing_numeric_module);
    if (status == CPKT_LUA_RUNTIME_OK) {
      status = cpkt_lua_runtime_require(runtime, "numeric_failure");
    }
    allocator.reject_growth = 0;
  } else if (status == CPKT_LUA_RUNTIME_OK &&
             (strcmp(item->name, "preload_registration") == 0 ||
              strcmp(item->name, "preload_arguments") == 0)) {
    status = exercise_preload(runtime,
                              strcmp(item->name, "preload_registration") == 0);
  } else if (status == CPKT_LUA_RUNTIME_OK) {
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
  if (!failed && strcmp(item->name, "numeric_error") == 0) {
    message = cpkt_lua_runtime_error(runtime);
    if (message == NULL || strstr(message, "1729") == NULL) {
      fprintf(stderr, "numeric error conversion lost the original value\n");
      failed = 1;
    }
  }
  if (!failed && (strcmp(item->name, "existing_wrap_limit") == 0 ||
                  strcmp(item->name, "native_upvalues") == 0)) {
    recovery = strcmp(item->name, "existing_wrap_limit") == 0
                   ? "policy_saved_wrap()"
                   : "policy_saved_resume(policy_saved_thread)";
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
