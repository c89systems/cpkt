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
    {"xhandler_limit",
     "xpcall(function() while true do end end, "
     "function(err) while true do end end)",
     1, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"xhandler_error_limit",
     "xpcall(function() error('ordinary error') end, "
     "function(err) while true do end end)",
     1, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"xhandler_normal",
     "local ok,err=xpcall(function(a,b) assert(a==7 and b==9); "
     "error('ordinary error') end, function(err) "
     "assert(err:match('ordinary error')); return 'handled' end,7,9); "
     "assert(not ok and err=='handled')",
     1, CPKT_LUA_RUNTIME_OK},
    {"gc_limit",
     "local t=setmetatable({}, {__gc=function() while true do end end}); "
     "t=nil; collectgarbage()",
     1, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"gc_coroutine_limit",
     "local co=coroutine.create(function() "
     "local t=setmetatable({}, {__gc=function() while true do end end}); "
     "t=nil; collectgarbage() end); coroutine.resume(co)",
     1, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"gc_finite",
     "local finalized=false; "
     "local t=setmetatable({}, {__gc=function() local n=0; "
     "for i=1,10 do n=n+i end; assert(n==55); finalized=true end}); "
     "t=nil; collectgarbage(); assert(finalized)",
     1, CPKT_LUA_RUNTIME_OK},
    {"gc_close_limit",
     "saved_gc=setmetatable({}, {__gc=function() while true do end end})", 1,
     CPKT_LUA_RUNTIME_OK},
    {"gc_close_after_limit",
     "saved_gc=setmetatable({}, {__gc=function() while true do end end}); "
     "while true do end",
     1, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"native_gc_hooks", "", 0, CPKT_LUA_RUNTIME_OK},
    {"coroutine_close_existing_limit", "", 0, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"coroutine_close_reopen_limit", "", 0, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"coroutine_close_cleared_limit", "", 0, CPKT_LUA_RUNTIME_OK},
    {"coroutine_close_normal_results", "", 0, CPKT_LUA_RUNTIME_OK},
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
    {"preload_arguments", "", 0, CPKT_LUA_RUNTIME_OK},
    {"preload_lua_upvalues", "", 0, CPKT_LUA_RUNTIME_OK},
    {"preload_c_upvalues", "", 0, CPKT_LUA_RUNTIME_OK},
    {"preload_c_allocation", "", 0, CPKT_LUA_RUNTIME_OK},
    {"reopen_resume_limit", "", 0, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"reopen_wrap_limit", "", 0, CPKT_LUA_RUNTIME_ERR_LIMIT},
    {"reopen_library_identity", "", 0, CPKT_LUA_RUNTIME_OK},
    {"construction_failure", "", 0, CPKT_LUA_RUNTIME_ERR_ALLOC}};

struct failure_allocator {
  int reject_growth;
};

static int native_hook_calls;
static int native_finalizer_hook_calls;

static void native_hook(cpkt_lua_state *state, cpkt_lua_debug *record) {
  (void)record;
  native_hook_calls += 1;
  cpkt_lua_getglobal(state, "inside_native_gc");
  if (cpkt_lua_toboolean(state, -1)) {
    native_finalizer_hook_calls += 1;
  }
  cpkt_lua_pop(state, 1);
}

static int exercise_native_gc_hooks(void) {
  cpkt_lua_state *state;
  const char *source;
  int status;
  state = cpkt_lua_l_newstate();
  if (state == NULL) {
    return 1;
  }
  cpkt_lua_l_openlibs(state);
  native_hook_calls = 0;
  native_finalizer_hook_calls = 0;
  cpkt_lua_sethook(state, native_hook, CPKT_LUA_MASKCOUNT, 1);
  source = "local t=setmetatable({}, {__gc=function() "
           "inside_native_gc=true; local n=0; "
           "for i=1,100 do n=n+i end; assert(n==5050); "
           "inside_native_gc=false; native_finalized=true end}); "
           "t=nil; collectgarbage(); assert(native_finalized)";
  status = cpkt_lua_l_loadbuffer(state, source, strlen(source), "native-gc");
  if (status == CPKT_LUA_OK) {
    status = cpkt_lua_pcall(state, 0, 0, 0);
  }
  cpkt_lua_close(state);
  return status != CPKT_LUA_OK || native_hook_calls == 0 ||
         native_finalizer_hook_calls != 0;
}

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

static cpkt_lua_runtime_status
exercise_preload_upvalues(cpkt_lua_runtime *runtime, int c_module) {
  cpkt_lua_runtime_status status;
  const char *source;
  source = "return true";
  status = cpkt_lua_runtime_register_lua_module(runtime, "tamper_lua",
                                                (const unsigned char *)source,
                                                strlen(source), "tamper-lua");
  if (status == CPKT_LUA_RUNTIME_OK) {
    status = cpkt_lua_runtime_register_c_module(runtime, "tamper_c",
                                                preload_c_module);
  }
  if (status != CPKT_LUA_RUNTIME_OK) {
    return status;
  }
  status =
      run_text(runtime, c_module ? "tamper_loader=package.preload.tamper_c; "
                                   "tamper_other=package.preload.tamper_lua; "
                                   "tamper_error='invalid C module opener'"
                                 : "tamper_loader=package.preload.tamper_lua; "
                                   "tamper_other=package.preload.tamper_c; "
                                   "tamper_error='invalid Lua preload chunk'");
  if (status != CPKT_LUA_RUNTIME_OK) {
    return status;
  }
  status = run_text(
      runtime,
      "local _,saved=debug.getupvalue(tamper_loader,1); "
      "local _,other=debug.getupvalue(tamper_other,1); "
      "local function reject(value) "
      "assert(debug.setupvalue(tamper_loader,1,value)); "
      "collectgarbage('collect'); "
      "local ok,err=pcall(tamper_loader); assert(not ok); "
      "assert(tostring(err):match(tamper_error)) end; "
      "reject(io.stdout); reject(nil); reject(17); reject('foreign'); "
      "reject(other); "
      "debug.setupvalue(tamper_loader,1,saved); assert(tamper_loader()==true)");
  return status == CPKT_LUA_RUNTIME_OK
             ? run_text(runtime, "assert(require('tamper_c')==true)")
             : status;
}

static cpkt_lua_runtime_status
exercise_reopened_libraries(cpkt_lua_runtime *runtime, const char *name) {
  cpkt_lua_runtime_status status;
  const char *preparation;
  const char *execution;
  if (strcmp(name, "reopen_library_identity") == 0) {
    preparation =
        "saved_create=coroutine.create; saved_resume=coroutine.resume; "
        "saved_wrap=coroutine.wrap; saved_pcall=pcall; saved_xpcall=xpcall; "
        "package.loaded.coroutine=nil; package.loaded._G=nil; "
        "package.loaded.debug=nil";
    execution = "assert(pcall==saved_pcall); assert(xpcall==saved_xpcall); "
                "assert(coroutine.create==saved_create); "
                "assert(coroutine.resume==saved_resume); "
                "assert(coroutine.wrap==saved_wrap); "
                "local ok,err=pcall(debug.sethook); assert(not ok and "
                "tostring(err):match('unavailable'))";
  } else {
    preparation = "saved_thread=coroutine.create(function() local sum=0; "
                  "for i=1,100000 do sum=sum+i end; return sum end); "
                  "package.loaded.coroutine=nil";
    execution = strcmp(name, "reopen_resume_limit") == 0
                    ? "assert(coroutine.resume(saved_thread))"
                    : "saved_reopened_wrap()";
  }
  status = run_text(runtime, preparation);
  if (status == CPKT_LUA_RUNTIME_OK) {
    status = cpkt_lua_runtime_openlibs(runtime);
  }
  if (status == CPKT_LUA_RUNTIME_OK) {
    status = cpkt_lua_runtime_open_libs(
        runtime, CPKT_LUA_RUNTIME_LIB_BASE | CPKT_LUA_RUNTIME_LIB_COROUTINE);
  }
  if (status == CPKT_LUA_RUNTIME_OK && strcmp(name, "reopen_wrap_limit") == 0) {
    status = run_text(
        runtime, "saved_reopened_wrap=coroutine.wrap(function() local sum=0; "
                 "for i=1,100000 do sum=sum+i end; return sum end)");
  }
  if (status == CPKT_LUA_RUNTIME_OK &&
      strcmp(name, "reopen_library_identity") != 0) {
    status = cpkt_lua_runtime_set_instruction_limit(runtime, 1000);
  }
  return status == CPKT_LUA_RUNTIME_OK ? run_text(runtime, execution) : status;
}

static cpkt_lua_runtime_status
exercise_coroutine_close(cpkt_lua_runtime *runtime, const char *name) {
  cpkt_lua_runtime_status status;
  const char *closer;
  const char *finish;
  int cleared;
  cleared = strcmp(name, "coroutine_close_cleared_limit") == 0;
  closer = "while true do end";
  if (cleared) {
    status = cpkt_lua_runtime_set_instruction_limit(runtime, 1000);
    if (status != CPKT_LUA_RUNTIME_OK) {
      return status;
    }
    closer = "local n=0; for i=1,10000 do n=n+i end; "
             "assert(n==50005000); close_finished=true";
  } else if (strcmp(name, "coroutine_close_normal_results") == 0) {
    closer = "error('native close error')";
  }
  status = run_text(runtime, "close_source=nil");
  if (status == CPKT_LUA_RUNTIME_OK) {
    status =
        cpkt_lua_runtime_set_global_string(runtime, "close_source", closer);
  }
  if (status == CPKT_LUA_RUNTIME_OK) {
    status = run_text(
        runtime, "local closer=assert(load(close_source)); "
                 "close_thread=coroutine.create(function() "
                 "local resource <close> = setmetatable({}, {__close=closer}); "
                 "coroutine.yield('ready') end); "
                 "local ok,value=coroutine.resume(close_thread); "
                 "assert(ok and value=='ready')");
  }
  if (status == CPKT_LUA_RUNTIME_OK &&
      strcmp(name, "coroutine_close_reopen_limit") == 0) {
    status = run_text(
        runtime, "saved_close=coroutine.close; package.loaded.coroutine=nil");
    if (status == CPKT_LUA_RUNTIME_OK) {
      status =
          cpkt_lua_runtime_open_libs(runtime, CPKT_LUA_RUNTIME_LIB_COROUTINE);
    }
    if (status == CPKT_LUA_RUNTIME_OK) {
      status = run_text(runtime, "assert(coroutine.close==saved_close)");
    }
  }
  if (status == CPKT_LUA_RUNTIME_OK) {
    status = cleared ? cpkt_lua_runtime_clear_instruction_limit(runtime)
                     : cpkt_lua_runtime_set_instruction_limit(runtime, 1000);
  }
  finish = "coroutine.close(close_thread)";
  if (cleared) {
    finish = "assert(coroutine.close(close_thread)); assert(close_finished)";
  } else if (strcmp(name, "coroutine_close_normal_results") == 0) {
    finish = "local ok,err=coroutine.close(close_thread); "
             "assert(not ok and err:match('native close error')); "
             "assert(coroutine.status(close_thread)=='dead'); "
             "assert(coroutine.close(close_thread))";
  }
  if (status == CPKT_LUA_RUNTIME_OK) {
    status = run_text(runtime, finish);
  }
  if (status == CPKT_LUA_RUNTIME_OK && cleared) {
    status = cpkt_lua_runtime_set_instruction_limit(runtime, 1000);
  }
  return status;
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

static void count_trace(void *context,
                        const cpkt_lua_runtime_trace_event *event) {
  unsigned *count;
  count = (unsigned *)context;
  if (event->source != NULL && event->source_size > 0 && event->line > 0) {
    *count += 1;
  }
}

static int exercise(const struct runtime_case *item, int observed) {
  cpkt_lua_runtime *runtime;
  cpkt_lua_runtime_status status;
  const char *message;
  int failed;
  const char *recovery;
  struct failure_allocator allocator;
  cpkt_lua_runtime_allocator_config config;
  int conversion_case;
  unsigned trace_count;

  runtime = NULL;
  trace_count = 0;
  if (strcmp(item->name, "native_gc_hooks") == 0) {
    return exercise_native_gc_hooks();
  }
  if (strcmp(item->name, "construction_failure") == 0) {
    status = cpkt_lua_runtime_new_with_limit(&runtime, 512);
    if (status != CPKT_LUA_RUNTIME_ERR_ALLOC || runtime != NULL) {
      cpkt_lua_runtime_free(runtime);
      fprintf(stderr, "limited construction did not fail safely\n");
      return 1;
    }
    status = cpkt_lua_runtime_new(&runtime);
    cpkt_lua_runtime_free(runtime);
    return status != CPKT_LUA_RUNTIME_OK;
  }
  failed = 0;
  conversion_case = strcmp(item->name, "error_conversion_oom") == 0;
  allocator.reject_growth = 0;
  memset(&config, 0, sizeof(config));
  config.user = &allocator;
  config.alloc_fn = allocate;
  config.realloc_fn = resize;
  config.free_fn = release;
  status = (conversion_case || strcmp(item->name, "preload_c_allocation") == 0)
               ? cpkt_lua_runtime_new_with_allocator(&runtime, &config)
               : cpkt_lua_runtime_new(&runtime);
  if (status != CPKT_LUA_RUNTIME_OK) {
    return 1;
  }
  status = cpkt_lua_runtime_openlibs(runtime);
  if (status == CPKT_LUA_RUNTIME_OK && observed) {
    status =
        cpkt_lua_runtime_set_trace_callback(runtime, count_trace, &trace_count);
  }
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
             (strcmp(item->name, "preload_lua_upvalues") == 0 ||
              strcmp(item->name, "preload_c_upvalues") == 0)) {
    status = exercise_preload_upvalues(
        runtime, strcmp(item->name, "preload_c_upvalues") == 0);
  } else if (status == CPKT_LUA_RUNTIME_OK &&
             strcmp(item->name, "preload_c_allocation") == 0) {
    allocator.reject_growth = 1;
    status = cpkt_lua_runtime_register_c_module(runtime, "allocation_failed",
                                                preload_c_module);
    allocator.reject_growth = 0;
    if (status == CPKT_LUA_RUNTIME_ERR_ALLOC) {
      status = run_text(runtime,
                        "assert(package.preload.allocation_failed==nil); "
                        "saved_preload=package.preload; package.preload=nil");
      if (status == CPKT_LUA_RUNTIME_OK) {
        status = cpkt_lua_runtime_register_c_module(
            runtime, "publication_failed", preload_c_module);
        if (status == CPKT_LUA_RUNTIME_ERR_RUNTIME) {
          status = run_text(runtime,
                            "package.preload=saved_preload; "
                            "assert(package.preload.publication_failed==nil)");
          if (status == CPKT_LUA_RUNTIME_OK) {
            status = cpkt_lua_runtime_register_c_module(
                runtime, "allocation_recovery", preload_c_module);
            if (status == CPKT_LUA_RUNTIME_OK) {
              status = run_text(runtime,
                                "assert(require('allocation_recovery')==true)");
            }
          }
        } else {
          status = CPKT_LUA_RUNTIME_ERR_RUNTIME;
        }
      }
    } else {
      status = CPKT_LUA_RUNTIME_ERR_RUNTIME;
    }
  } else if (status == CPKT_LUA_RUNTIME_OK &&
             strncmp(item->name, "coroutine_close_", 16) == 0) {
    status = exercise_coroutine_close(runtime, item->name);
  } else if (status == CPKT_LUA_RUNTIME_OK &&
             strncmp(item->name, "reopen_", 7) == 0) {
    status = exercise_reopened_libraries(runtime, item->name);
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
  if (strncmp(item->name, "gc_close_", 9) == 0) {
    cpkt_lua_runtime_free(runtime);
    return failed || (observed && trace_count == 0);
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
  if (!failed &&
      (item->limited || strncmp(item->name, "coroutine_close_", 16) == 0)) {
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
  return failed || (observed && trace_count == 0);
}

struct trace_capture {
  cpkt_lua_runtime *runtime;
  const char *source;
  unsigned events;
  unsigned matching;
  unsigned lines;
  int action;
  int failed;
  struct trace_capture *replacement;
};

static void capture_trace(void *context,
                          const cpkt_lua_runtime_trace_event *event);

static void replacement_trace(void *context,
                              const cpkt_lua_runtime_trace_event *event) {
  capture_trace(context, event);
}

struct warning_trace_install {
  struct trace_capture *capture;
  cpkt_lua_runtime_status status;
  unsigned calls;
};

static void install_trace_from_warning(void *context, const char *message,
                                       int to_continue) {
  struct warning_trace_install *install;
  (void)message;
  (void)to_continue;
  install = (struct warning_trace_install *)context;
  install->calls += 1;
  install->status = cpkt_lua_runtime_set_trace_callback(
      install->capture->runtime, capture_trace, install->capture);
}

static int install_trace_from_module(void *state) {
  struct warning_trace_install *install;
  install = (struct warning_trace_install *)cpkt_lua_runtime_context_from_state(
      state);
  install->status = cpkt_lua_runtime_set_trace_callback(
      install->capture->runtime, capture_trace, install->capture);
  return 0;
}

static void capture_trace(void *context,
                          const cpkt_lua_runtime_trace_event *event) {
  struct trace_capture *capture;
  cpkt_lua_runtime *runtime;
  static const unsigned char source[] = "return true";
  capture = (struct trace_capture *)context;
  runtime = capture->runtime;
  capture->events += 1;
  if (event->source == NULL || event->line <= 0) {
    capture->failed = 1;
    return;
  }
  if (event->source_size != strlen(capture->source) ||
      memcmp(event->source, capture->source, event->source_size) != 0) {
    return;
  }
  capture->matching += 1;
  if (event->line <= 8) {
    capture->lines |= 1U << (event->line - 1);
  }
  if (capture->action == 1) {
    capture->failed |= cpkt_lua_runtime_set_trace_callback(
                           runtime, NULL, NULL) != CPKT_LUA_RUNTIME_OK;
  } else if (capture->action == 2) {
    capture->failed |= cpkt_lua_runtime_set_trace_callback(
                           runtime, replacement_trace, capture->replacement) !=
                       CPKT_LUA_RUNTIME_OK;
  } else if (capture->action == 3) {
#define REJECT_TRACE_CALL(call)                                                \
  capture->failed |= (call) != CPKT_LUA_RUNTIME_ERR_ARG
    REJECT_TRACE_CALL(cpkt_lua_runtime_openlibs(runtime));
    REJECT_TRACE_CALL(cpkt_lua_runtime_open_libs(runtime, 0));
    REJECT_TRACE_CALL(cpkt_lua_runtime_set_traceback(runtime, 1));
    REJECT_TRACE_CALL(cpkt_lua_runtime_set_instruction_limit(runtime, 1));
    REJECT_TRACE_CALL(cpkt_lua_runtime_clear_instruction_limit(runtime));
    REJECT_TRACE_CALL(cpkt_lua_runtime_set_package_path(runtime, ""));
    REJECT_TRACE_CALL(cpkt_lua_runtime_prepend_package_path(runtime, ""));
    REJECT_TRACE_CALL(cpkt_lua_runtime_set_package_cpath(runtime, ""));
    REJECT_TRACE_CALL(cpkt_lua_runtime_prepend_package_cpath(runtime, ""));
    REJECT_TRACE_CALL(cpkt_lua_runtime_set_global_string(runtime, "x", ""));
    REJECT_TRACE_CALL(cpkt_lua_runtime_set_global_boolean(runtime, "x", 1));
    REJECT_TRACE_CALL(cpkt_lua_runtime_set_global_number(runtime, "x", 1));
    REJECT_TRACE_CALL(cpkt_lua_runtime_set_global_integer(runtime, "x", 1));
    REJECT_TRACE_CALL(
        cpkt_lua_runtime_register_c_module(runtime, "x", preload_c_module));
    REJECT_TRACE_CALL(cpkt_lua_runtime_register_lua_module(
        runtime, "x", source, sizeof(source) - 1, "x"));
    REJECT_TRACE_CALL(cpkt_lua_runtime_require(runtime, "x"));
    REJECT_TRACE_CALL(
        cpkt_lua_runtime_run_file(runtime, "missing.lua", 0, NULL, 0));
    REJECT_TRACE_CALL(cpkt_lua_runtime_run_buffer(
        runtime, source, sizeof(source) - 1, "x", 0, NULL, 0));
#undef REJECT_TRACE_CALL
    cpkt_lua_runtime_free(runtime);
    cpkt_lua_runtime_set_context(runtime, NULL);
    cpkt_lua_runtime_set_warning_callback(runtime, NULL, NULL);
    cpkt_lua_runtime_clear_error(runtime);
    capture->failed |= cpkt_lua_runtime_context(runtime) != capture;
    capture->failed |= cpkt_lua_runtime_error(runtime)[0] != '\0';
  }
  capture->action = 0;
}

static int exercise_trace_observer(void) {
  static const unsigned char source[] =
      "local value = 1\nvalue = value + 1\nassert(value == 2)\n";
  static const char *child_setup =
      "saved=coroutine.create(assert(load('local "
      "x=1\\ncoroutine.yield()\\nx=x+1', '@child.lua'))); "
      "wrapped=coroutine.wrap(assert(load('local x=1\\nx=x+1', "
      "'@child.lua'))); "
      "assert(coroutine.resume(saved))";
  static const char *finalizer_setup =
      "saved_gc=setmetatable({}, {__gc=assert(load('local "
      "x=1\\nx=x+1\\nassert(x==2)', '@finalizer.lua'))})";
  cpkt_lua_runtime *runtime;
  struct trace_capture capture;
  struct trace_capture second;
  struct warning_trace_install install;
  unsigned before;
  int limited;
  int failed;
  runtime = NULL;
  memset(&capture, 0, sizeof(capture));
  memset(&second, 0, sizeof(second));
  memset(&install, 0, sizeof(install));
  failed = 0;
#define TRACE_CHECK(condition)                                                 \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "trace observer check failed at line %d: %s\n",          \
              __LINE__, #condition);                                           \
      failed = 1;                                                              \
      goto done;                                                               \
    }                                                                          \
  } while (0)
  TRACE_CHECK(cpkt_lua_runtime_set_trace_callback(
                  NULL, capture_trace, &capture) == CPKT_LUA_RUNTIME_ERR_ARG);
  TRACE_CHECK(cpkt_lua_runtime_new(&runtime) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(cpkt_lua_runtime_openlibs(runtime) == CPKT_LUA_RUNTIME_OK);
  capture.runtime = runtime;
  capture.source = "@trace.lua";
  second.runtime = runtime;
  second.source = "@trace.lua";
  install.capture = &capture;
  cpkt_lua_runtime_set_warning_callback(runtime, install_trace_from_warning,
                                        &install);
  TRACE_CHECK(run_text(runtime,
                       "coroutine.wrap(function()\nwarn('observer')"
                       "\nlocal x=1\nx=x+1\nend)()") == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(install.calls > 0 && install.status == CPKT_LUA_RUNTIME_ERR_ARG);
  TRACE_CHECK(capture.events == 0);
  cpkt_lua_runtime_set_context(runtime, &install);
  TRACE_CHECK(cpkt_lua_runtime_register_c_module(runtime, "trace_install",
                                                 install_trace_from_module) ==
              CPKT_LUA_RUNTIME_OK);
  install.status = CPKT_LUA_RUNTIME_OK;
  TRACE_CHECK(cpkt_lua_runtime_require(runtime, "trace_install") ==
              CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(install.status == CPKT_LUA_RUNTIME_ERR_ARG &&
              capture.events == 0);
  for (limited = 0; limited <= 1; ++limited) {
    if (limited) {
      TRACE_CHECK(cpkt_lua_runtime_set_instruction_limit(runtime, 1000) ==
                  CPKT_LUA_RUNTIME_OK);
    }
    capture.lines = 0;
    TRACE_CHECK(cpkt_lua_runtime_set_trace_callback(
                    runtime, capture_trace, &capture) == CPKT_LUA_RUNTIME_OK);
    TRACE_CHECK(cpkt_lua_runtime_run_buffer(runtime, source, sizeof(source) - 1,
                                            "@trace.lua", 0, NULL,
                                            0) == CPKT_LUA_RUNTIME_OK);
    TRACE_CHECK(capture.lines == 7 && !capture.failed);
    TRACE_CHECK(cpkt_lua_runtime_set_trace_callback(runtime, NULL, &second) ==
                CPKT_LUA_RUNTIME_OK);
    before = capture.events;
    TRACE_CHECK(run_text(runtime, "local x=1; x=x+1") == CPKT_LUA_RUNTIME_OK);
    TRACE_CHECK(capture.events == before);
  }
  TRACE_CHECK(cpkt_lua_runtime_clear_instruction_limit(runtime) ==
              CPKT_LUA_RUNTIME_OK);
  capture.source = "=named chunk";
  capture.lines = 0;
  TRACE_CHECK(cpkt_lua_runtime_set_trace_callback(
                  runtime, capture_trace, &capture) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(cpkt_lua_runtime_run_buffer(runtime, source, sizeof(source) - 1,
                                          "=named chunk", 0, NULL,
                                          0) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(capture.lines == 7);
  capture.source =
      "=a deliberately long source identifier that must remain complete in the "
      "observer event instead of using Lua's shortened display name";
  capture.lines = 0;
  TRACE_CHECK(cpkt_lua_runtime_run_buffer(runtime, source, sizeof(source) - 1,
                                          capture.source, 0, NULL,
                                          0) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(capture.lines == 7);
  TRACE_CHECK(cpkt_lua_runtime_set_trace_callback(runtime, NULL, NULL) ==
              CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(run_text(runtime, child_setup) == CPKT_LUA_RUNTIME_OK);
  capture.source = "@child.lua";
  capture.lines = 0;
  TRACE_CHECK(cpkt_lua_runtime_set_trace_callback(
                  runtime, capture_trace, &capture) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(run_text(runtime,
                       "assert(coroutine.resume(saved)); wrapped(); "
                       "assert(coroutine.resume(coroutine.create(assert(load('"
                       "local x=1\\nx=x+1', '@child.lua')))))") ==
              CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(capture.lines == 7);
  capture.source = "@trace.lua";
  capture.matching = 0;
  capture.action = 2;
  capture.replacement = &second;
  TRACE_CHECK(cpkt_lua_runtime_run_buffer(runtime, source, sizeof(source) - 1,
                                          "@trace.lua", 0, NULL,
                                          0) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(capture.matching == 1 && second.matching == 2);
  capture.source = "@child.lua";
  capture.action = 1;
  capture.matching = 0;
  TRACE_CHECK(cpkt_lua_runtime_set_trace_callback(
                  runtime, capture_trace, &capture) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(
      run_text(runtime,
               "local f=assert(load('local x=1\\nx=x+1', '@child.lua')); "
               "coroutine.wrap(f)(); coroutine.wrap(f)()") ==
      CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(capture.matching == 1);
  before = capture.events;
  TRACE_CHECK(run_text(runtime, "assert(true)") == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(capture.events == before);
  TRACE_CHECK(cpkt_lua_runtime_set_instruction_limit(runtime, 100) ==
              CPKT_LUA_RUNTIME_OK);
  /* A line event must not consume another unit of the instruction budget. */
  TRACE_CHECK(
      run_text(runtime, "progress=0\nfor i=1,1000 do\nprogress=i\nend") ==
      CPKT_LUA_RUNTIME_ERR_LIMIT);
  TRACE_CHECK(run_text(runtime, "unobserved_progress=progress") ==
              CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(cpkt_lua_runtime_set_trace_callback(
                  runtime, capture_trace, &capture) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(
      run_text(runtime, "progress=0\nfor i=1,1000 do\nprogress=i\nend") ==
      CPKT_LUA_RUNTIME_ERR_LIMIT);
  TRACE_CHECK(run_text(runtime, "assert(progress==unobserved_progress)") ==
              CPKT_LUA_RUNTIME_OK);
  capture.source = "reentry";
  capture.action = 3;
  cpkt_lua_runtime_set_context(runtime, &capture);
  TRACE_CHECK(cpkt_lua_runtime_set_instruction_limit(runtime, 100) ==
              CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(cpkt_lua_runtime_set_trace_callback(
                  runtime, capture_trace, &capture) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(cpkt_lua_runtime_run_buffer(
                  runtime, (const unsigned char *)"while true do\nend",
                  sizeof("while true do\nend") - 1, "reentry", 0, NULL,
                  0) == CPKT_LUA_RUNTIME_ERR_LIMIT);
  TRACE_CHECK(!capture.failed && capture.action == 0);
  capture.source = "remove-budget";
  capture.matching = 0;
  capture.action = 1;
  TRACE_CHECK(cpkt_lua_runtime_run_buffer(
                  runtime, (const unsigned char *)"while true do\nend",
                  sizeof("while true do\nend") - 1, capture.source, 0, NULL,
                  0) == CPKT_LUA_RUNTIME_ERR_LIMIT);
  TRACE_CHECK(capture.matching == 1);
  capture.source = "replace-budget";
  capture.matching = 0;
  capture.action = 2;
  second.source = capture.source;
  second.matching = 0;
  TRACE_CHECK(cpkt_lua_runtime_set_trace_callback(
                  runtime, capture_trace, &capture) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(cpkt_lua_runtime_run_buffer(
                  runtime, (const unsigned char *)"while true do\nend",
                  sizeof("while true do\nend") - 1, capture.source, 0, NULL,
                  0) == CPKT_LUA_RUNTIME_ERR_LIMIT);
  TRACE_CHECK(capture.matching == 1 && second.matching > 0);
  TRACE_CHECK(cpkt_lua_runtime_set_trace_callback(
                  runtime, capture_trace, &capture) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(cpkt_lua_runtime_clear_instruction_limit(runtime) ==
              CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(run_text(runtime, "error('observed script error')") ==
              CPKT_LUA_RUNTIME_ERR_RUNTIME);
  TRACE_CHECK(
      strstr(cpkt_lua_runtime_error(runtime), "observed script error") != NULL);
  before = capture.events;
  TRACE_CHECK(run_text(runtime, "local =") == CPKT_LUA_RUNTIME_ERR_LOAD);
  TRACE_CHECK(capture.events == before);
  TRACE_CHECK(cpkt_lua_runtime_set_trace_callback(
                  runtime, capture_trace, &capture) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(run_text(runtime, "assert(true)") == CPKT_LUA_RUNTIME_OK);
  capture.source = "@finalizer.lua";
  for (limited = 0; limited <= 1; ++limited) {
    if (limited) {
      TRACE_CHECK(cpkt_lua_runtime_set_instruction_limit(runtime, 1000) ==
                  CPKT_LUA_RUNTIME_OK);
    }
    capture.lines = 0;
    TRACE_CHECK(run_text(runtime, finalizer_setup) == CPKT_LUA_RUNTIME_OK);
    TRACE_CHECK(run_text(runtime, "saved_gc=nil; collectgarbage()") ==
                CPKT_LUA_RUNTIME_OK);
    TRACE_CHECK(capture.lines == 7);
  }
  capture.lines = 0;
  TRACE_CHECK(run_text(runtime, finalizer_setup) == CPKT_LUA_RUNTIME_OK);
  TRACE_CHECK(run_text(runtime,
                       "warning_gc=setmetatable({}, {__gc=function() "
                       "error('close warning') end})") == CPKT_LUA_RUNTIME_OK);
  before = install.calls;
  cpkt_lua_runtime_free(runtime);
  runtime = NULL;
  TRACE_CHECK(capture.lines == 7 && !capture.failed);
  TRACE_CHECK(install.calls > before &&
              install.status == CPKT_LUA_RUNTIME_ERR_ARG);
done:
  cpkt_lua_runtime_free(runtime);
#undef TRACE_CHECK
  return failed;
}

static int exercise_trace_limits(void) {
  cpkt_lua_runtime *runtime;
  unsigned count;
  size_t index;
  int failed;
  const char *nested;
  const char *cyclic;
  /* Reuse the existing budget/finalizer regressions with observation enabled.
   */
  for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
    if ((cases[index].limited ||
         strncmp(cases[index].name, "coroutine_close_", 16) == 0 ||
         strcmp(cases[index].name, "existing_wrap_limit") == 0) &&
        exercise(&cases[index], 1)) {
      return 1;
    }
  }
  runtime = NULL;
  count = 0;
  if (cpkt_lua_runtime_new(&runtime) != CPKT_LUA_RUNTIME_OK) {
    return 1;
  }
  failed = cpkt_lua_runtime_openlibs(runtime) != CPKT_LUA_RUNTIME_OK ||
           cpkt_lua_runtime_set_instruction_limit(runtime, 1000) !=
               CPKT_LUA_RUNTIME_OK ||
           cpkt_lua_runtime_set_trace_callback(runtime, count_trace, &count) !=
               CPKT_LUA_RUNTIME_OK;
  nested = "local function f(n) if n>0 then "
           "coroutine.resume(coroutine.create(function() f(n-1) end)) "
           "else while true do end end end; f(8)";
  cyclic = "local a,b; a=coroutine.create(function() while true do "
           "coroutine.resume(b) end end); "
           "b=coroutine.create(function() coroutine.resume(a) end); "
           "coroutine.resume(a)";
  if (!failed) {
    failed = run_text(runtime, nested) != CPKT_LUA_RUNTIME_ERR_LIMIT ||
             run_text(runtime, cyclic) != CPKT_LUA_RUNTIME_ERR_LIMIT ||
             count == 0;
  }
  cpkt_lua_runtime_free(runtime);
  return failed;
}

int main(int argc, char **argv) {
  size_t index;
  if (argc != 2) {
    return 2;
  }
  if (strcmp(argv[1], "trace_observer") == 0) {
    return exercise_trace_observer();
  }
  if (strcmp(argv[1], "trace_limits") == 0) {
    return exercise_trace_limits();
  }
  for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
    if (strcmp(argv[1], cases[index].name) == 0) {
      return exercise(&cases[index], 0);
    }
  }
  fprintf(stderr, "unknown runtime case: %s\n", argv[1]);
  return 2;
}
