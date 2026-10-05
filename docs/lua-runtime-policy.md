# Lua runtime execution policy

The full `<cpkt/lua.h>` facade preserves Lua's public C API. The separate
`<cpkt/lua_runtime.h>` facade owns a VM and adds embedding policy, including
allocation caps, warning callbacks and an optional instruction limit.

With a limit enabled, exhaustion ends the host invocation with
`CPKT_LUA_RUNTIME_ERR_LIMIT`. Lua `pcall` and `xpcall` cannot repeatedly swallow
that failure. Their normal error results, argument forwarding and yielding
continuations remain native Lua behavior. Each host invocation receives a fresh
instruction budget; after exhaustion, the next invocation can run without
clearing or changing that budget. This is a VM instruction policy, not a wall
clock timeout or a security sandbox; trusted C modules can block, execute native
code or alter VM policy. The counter is shared across all coroutine switches,
including short yields. With a limit enabled, the VM calls the accounting hook
for each instruction; unlimited execution does not install that hook.

Lua error handlers and finalizers are included in this policy. After exhaustion,
the runtime's `xpcall` handler returns the limit error without calling the Lua
handler. Bundled Lua has an opt-in GC patch that preserves instruction hooks
during finalizers for the runtime's private hook-mask bit. Ordinary native Lua
hooks keep upstream finalizer suppression. The patch changes no public types or
symbols, and its source and shared private header are tracked recipe inputs.
`cpkt_lua_runtime_free()` grants finalizers a fresh configured budget while the
limit remains enabled. Finalizer errors use the warning callback. Clearing the
limit also removes accounting during finalization.

The runtime handle lives in Lua's embedder extra space, outside the registry that
`debug.getregistry()` exposes. Native C module openers borrow a VM state and must
leave that extra space intact. Modules should use their own registry keys or
userdata for module state. `cpkt_lua_runtime_context_from_state()` supplies the
embedding context without changing ownership. C89 module callbacks can use
`<cpkt/lua.h>` with the borrowed state; upstream headers require C99 or newer.

Coroutine wrappers install the runtime's hook and delegate execution, stack
growth, return/yield transfers and error closure to Lua's own coroutine library.
`coroutine.close` applies the current policy to a suspended thread before its
Lua `<close>` handlers run, including threads created before the limit changed.
Clearing the limit removes the thread's accounting hook on its next close.
An errored wrapped coroutine closes its `<close>` variables before propagating
the resulting error. Wrappers created before a limit change receive the current
policy on their next invocation. Opening standard libraries again reinstalls
these wrappers and the protected-call guards, including after clearing
`package.loaded` entries; repeated opens preserve wrapper identity.

C and Lua module registration writes directly to `package.preload`, bypassing
its `__newindex` metamethod. This prevents an assignment callback from publishing
a Lua loader and then raising an error while its copied source is being freed.
The runtime keeps successful source copies and C opener records until runtime
destruction, including replaced registrations; failed registration releases its
new record. Loaders check upvalue type and membership in their runtime-owned
records before dereferencing data or calling an opener. Substituting foreign
userdata, nil, or a record of the wrong kind raises a Lua error. Restoring a
retained valid record remains safe after garbage collection. Lua loaders
forward all arguments to the source chunk, including the module name and loader
data from `require`. Compilation happens when the loader is invoked.

The bounded `lua_runtime_contract_*` tests exercise both static and shared
libraries on supported Linux runners and native Darwin. They cover large value
counts, coroutine error cleanup, protected calls and yields, registry/upvalue
mutation, changed limits, shared budgets, protected error conversion under
allocation failure and recovery, preload publication, replacement and loader
argument forwarding, loader-upvalue substitution, reopened standard libraries
and failed C registration recovery, bounded error handlers, finalization during
collection and destruction, and ordinary native GC-hook behavior. A timeout or
process abort is a failing result.
