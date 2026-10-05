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

The runtime handle lives in Lua's embedder extra space, outside the registry that
`debug.getregistry()` exposes. Native C module openers borrow a VM state and must
leave that extra space intact. Modules should use their own registry keys or
userdata for module state. `cpkt_lua_runtime_context_from_state()` supplies the
embedding context without changing ownership. C89 module callbacks can use
`<cpkt/lua.h>` with the borrowed state; upstream headers require C99 or newer.

Coroutine wrappers install the runtime's hook and delegate execution, stack
growth, return/yield transfers and error closure to Lua's own coroutine library.
An errored wrapped coroutine closes its `<close>` variables before propagating
the resulting error. Wrappers created before a limit change receive the current
policy on their next invocation.

C and Lua module registration writes directly to `package.preload`, bypassing
its `__newindex` metamethod. This prevents an assignment callback from publishing
a Lua loader and then raising an error while its copied source is being freed.
The runtime keeps successful source copies until runtime destruction, including
replaced registrations; failed registration releases its new copy. Lua loaders
forward all arguments to the source chunk, including the module name and loader
data from `require`. Compilation happens when the loader is invoked.

The bounded `lua_runtime_contract_*` tests exercise both static and shared
libraries on supported Linux runners and native Darwin. They cover large value
counts, coroutine error cleanup, protected calls and yields, registry/upvalue
mutation, changed limits, shared budgets, protected error conversion under
allocation failure and recovery, preload publication, replacement and loader
argument forwarding. A timeout or process abort is a failing result.
