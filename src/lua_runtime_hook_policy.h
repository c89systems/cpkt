#ifndef CPKT_LUA_RUNTIME_HOOK_POLICY_H
#define CPKT_LUA_RUNTIME_HOOK_POLICY_H

/* Private opt-in understood by the bundled Lua GC patch. No public hook event
 * uses this bit; ordinary Lua hooks retain native finalizer suppression. */
#define CPKT_LUA_RUNTIME_MASK_FINALIZERS 16

#endif
