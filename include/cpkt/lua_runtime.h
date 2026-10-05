#ifndef CPKT_LUA_RUNTIME_H
#define CPKT_LUA_RUNTIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque strict-C89 Lua runtime handle.
 *
 * This facade owns a Lua state and exposes only embedding policy operations.
 * It deliberately does not expose upstream Lua stack, value, userdata, or
 * metatable APIs. Consumers that need the full Lua C API should link the
 * strict-C89 `cpkt::lua` target and include <cpkt/lua.h> instead.
 */
typedef struct cpkt_lua_runtime cpkt_lua_runtime;

/**
 * Receives one Lua warning fragment. `message` is borrowed until return.
 * A nonzero `to_continue` means the next fragment belongs to this warning.
 * The callback and context remain caller-owned. Do not longjmp across Lua.
 */
typedef void (*cpkt_lua_runtime_warning_fn)(void *context, const char *message,
                                            int to_continue);

/**
 * Result codes returned by strict Lua runtime facade operations.
 */
typedef enum cpkt_lua_runtime_status {
  /** Operation completed successfully. */
  CPKT_LUA_RUNTIME_OK = 0,
  /** Invalid argument, missing required callback, or invalid handle. */
  CPKT_LUA_RUNTIME_ERR_ARG = 1,
  /** Allocation failed, including a runtime-managed byte cap being reached. */
  CPKT_LUA_RUNTIME_ERR_ALLOC = 2,
  /** Lua source failed to load or compile. */
  CPKT_LUA_RUNTIME_ERR_LOAD = 3,
  /** Lua code loaded but failed while running. */
  CPKT_LUA_RUNTIME_ERR_RUNTIME = 4,
  /** Configured Lua VM instruction limit was reached. */
  CPKT_LUA_RUNTIME_ERR_LIMIT = 5
} cpkt_lua_runtime_status;

/**
 * Execution flags for file and buffer runs.
 */
typedef enum cpkt_lua_runtime_flags {
  /** Open all Lua standard libraries before executing the script. */
  CPKT_LUA_RUNTIME_OPEN_LIBS = 1
} cpkt_lua_runtime_flags;

/**
 * Bitmask for selecting Lua standard libraries.
 *
 * Use with `cpkt_lua_runtime_open_libs()`. The package library is required for
 * `require`, package search paths, native module cpaths, and preload modules.
 */
typedef enum cpkt_lua_runtime_libs {
  /** Lua base library. */
  CPKT_LUA_RUNTIME_LIB_BASE = 1,
  /** Lua package library. */
  CPKT_LUA_RUNTIME_LIB_PACKAGE = 2,
  /** Lua coroutine library. */
  CPKT_LUA_RUNTIME_LIB_COROUTINE = 4,
  /** Lua table library. */
  CPKT_LUA_RUNTIME_LIB_TABLE = 8,
  /** Lua I/O library. */
  CPKT_LUA_RUNTIME_LIB_IO = 16,
  /** Lua OS library. */
  CPKT_LUA_RUNTIME_LIB_OS = 32,
  /** Lua string library. */
  CPKT_LUA_RUNTIME_LIB_STRING = 64,
  /** Lua UTF-8 library. */
  CPKT_LUA_RUNTIME_LIB_UTF8 = 128,
  /** Lua math library. */
  CPKT_LUA_RUNTIME_LIB_MATH = 256,
  /** Lua debug library. `debug.sethook` is disabled by this strict facade. */
  CPKT_LUA_RUNTIME_LIB_DEBUG = 512,
  /** All standard libraries supported by this facade. */
  CPKT_LUA_RUNTIME_LIB_ALL = 1023
} cpkt_lua_runtime_libs;

/**
 * C module opener callback used by `cpkt_lua_runtime_register_c_module()`.
 *
 * The pointer is the upstream Lua VM state as an opaque `void *` so this public
 * header remains C89-compatible and does not include Lua headers. C89 callbacks
 * may include <cpkt/lua.h> and cast the borrowed pointer to cpkt_lua_state *.
 * Callbacks using upstream Lua headers require a C99-or-newer translation unit.
 * The borrowed state stores facade policy in Lua extra space; module openers
 * must leave that embedder-owned storage intact.
 */
typedef int (*cpkt_lua_runtime_c_module_open_fn)(void *lua_state);

/**
 * Allocates a new block from a runtime allocator.
 *
 * Return NULL on allocation failure. The facade never calls this callback with
 * `size == 0`.
 */
typedef void *(*cpkt_lua_runtime_alloc_fn)(void *user, size_t size);

/**
 * Resizes a block from a runtime allocator.
 *
 * This callback is optional. If omitted, the facade allocates a new block,
 * copies `min(old_size, new_size)` bytes, and frees the old block through the
 * same allocator. Return NULL on allocation failure.
 */
typedef void *(*cpkt_lua_runtime_realloc_fn)(void *user, void *ptr,
                                             size_t old_size, size_t new_size);

/**
 * Frees a block from a runtime allocator.
 *
 * `size` is the facade's best-known allocation size for the block. The callback
 * must tolerate `ptr == NULL` only if the allocator itself chooses to support
 * that convention; the facade does not intentionally free NULL blocks.
 */
typedef void (*cpkt_lua_runtime_free_fn)(void *user, void *ptr, size_t size);

/**
 * Allocator configuration for `cpkt_lua_runtime_new_with_allocator()`.
 *
 * If `alloc_fn` and `free_fn` are both NULL, the facade uses the default heap
 * allocator and `realloc_fn` must also be NULL. If either `alloc_fn` or
 * `free_fn` is provided, both must be provided; `realloc_fn` is then optional.
 * `max_bytes == 0` means no facade-enforced byte cap.
 */
typedef struct cpkt_lua_runtime_allocator_config {
  /** Opaque pointer passed to all allocator callbacks. */
  void *user;
  /** Required allocation callback when using a custom allocator. */
  cpkt_lua_runtime_alloc_fn alloc_fn;
  /** Optional resize callback. */
  cpkt_lua_runtime_realloc_fn realloc_fn;
  /** Required free callback when using a custom allocator. */
  cpkt_lua_runtime_free_fn free_fn;
  /** Maximum runtime-managed bytes, or 0 for unrestricted. */
  size_t max_bytes;
} cpkt_lua_runtime_allocator_config;

/**
 * Returns the static upstream LUA_VERSION string (major/minor, e.g. "Lua 5.5").
 *
 * This does not include the patch release; consult the bundle dependency pins
 * for that version. The caller must not free the returned string.
 */
const char *cpkt_lua_runtime_lua_version(void);

/**
 * Returns the static strict facade API revision string (currently "1").
 *
 * This is independent of the bundle release and shared-library ABI major.
 * The caller must not free the returned string.
 */
const char *cpkt_lua_runtime_facade_version(void);

/**
 * Creates an unrestricted runtime with the default heap allocator.
 *
 * On success, writes a new handle to `*out`. The caller owns the handle and
 * must free it with `cpkt_lua_runtime_free()`.
 */
cpkt_lua_runtime_status cpkt_lua_runtime_new(cpkt_lua_runtime **out);

/**
 * Creates a runtime with a byte cap for runtime-managed allocations.
 *
 * A max_bytes value of 0 means unrestricted. A cap-induced allocation failure
 * returns CPKT_LUA_RUNTIME_ERR_ALLOC, including during construction.
 */
cpkt_lua_runtime_status cpkt_lua_runtime_new_with_limit(cpkt_lua_runtime **out,
                                                        size_t max_bytes);

/**
 * Creates a runtime with caller-provided allocation callbacks.
 *
 * If alloc_fn and free_fn are NULL, the default heap allocator is used and
 * realloc_fn must also be NULL. If either alloc_fn or free_fn is provided, both
 * must be provided; realloc_fn is optional. The allocator is used for
 * facade-owned memory and as the Lua state allocation hook.
 */
cpkt_lua_runtime_status cpkt_lua_runtime_new_with_allocator(
    cpkt_lua_runtime **out, const cpkt_lua_runtime_allocator_config *allocator);

/**
 * Frees a runtime.
 *
 * Passing NULL is allowed. Any stored error string, registered preload chunks,
 * and Lua-state memory are released through the runtime allocator.
 */
void cpkt_lua_runtime_free(cpkt_lua_runtime *runtime);

/**
 * Stores an embedder-owned context pointer.
 *
 * The facade never dereferences or frees this pointer.
 */
void cpkt_lua_runtime_set_context(cpkt_lua_runtime *runtime, void *context);

/**
 * Replaces the warning sink for this runtime. NULL suppresses warnings.
 * Lua emits fragments; this facade passes each fragment through unchanged.
 * Replacement during a warning takes effect on the next fragment. The
 * callback can run during script execution and runtime destruction.
 */
void cpkt_lua_runtime_set_warning_callback(cpkt_lua_runtime *runtime,
                                           cpkt_lua_runtime_warning_fn callback,
                                           void *context);

/**
 * Returns the embedder-owned context pointer currently stored on the runtime.
 */
void *cpkt_lua_runtime_context(const cpkt_lua_runtime *runtime);

/**
 * Retrieves the embedder context from a C module opener callback state.
 */
void *cpkt_lua_runtime_context_from_state(void *lua_state);

/**
 * Opens all standard libraries supported by this facade.
 *
 * If the debug library is opened, `debug.sethook` is replaced with a facade
 * error function so Lua code cannot clear or replace instruction-limit hooks.
 */
cpkt_lua_runtime_status cpkt_lua_runtime_openlibs(cpkt_lua_runtime *runtime);

/**
 * Opens the selected standard-library bitmask.
 *
 * If the debug library is selected, `debug.sethook` is replaced with a facade
 * error function so Lua code cannot clear or replace instruction-limit hooks.
 */
cpkt_lua_runtime_status cpkt_lua_runtime_open_libs(cpkt_lua_runtime *runtime,
                                                   int libs);

/**
 * Enables or disables traceback text in runtime errors.
 */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_traceback(cpkt_lua_runtime *runtime, int enabled);

/**
 * Installs a count hook that interrupts Lua execution after approximately the
 * requested number of VM instruction steps with CPKT_LUA_RUNTIME_ERR_LIMIT.
 *
 * instruction_count must be positive; use
 * cpkt_lua_runtime_clear_instruction_limit() to disable it. This is not a
 * wall-clock timeout and cannot interrupt a blocking C call. Exhaustion is
 * terminal for the host run: Lua pcall/xpcall cannot suppress the limit.
 * Ordinary protected-call results and coroutine yields retain native behavior.
 * This embedding policy does not make unrestricted Lua a security sandbox.
 *
 * The budget is shared across the main thread and coroutines for each host
 * invocation. Yielding or resuming a coroutine does not replenish it. A new
 * host invocation starts a fresh budget. While enabled, instruction accounting
 * runs a count hook for every VM instruction.
 * Lua finalizers participate in the budget. Destruction uses a fresh budget
 * for finalizers when the limit remains enabled; finalizer failures reach the
 * configured warning callback. After exhaustion, xpcall skips the Lua error
 * handler so it cannot run with instruction accounting suspended.
 */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_instruction_limit(cpkt_lua_runtime *runtime,
                                       int instruction_count);

/**
 * Removes any configured instruction limit.
 */
cpkt_lua_runtime_status
cpkt_lua_runtime_clear_instruction_limit(cpkt_lua_runtime *runtime);

/**
 * Replaces the runtime Lua module search path.
 *
 * Requires the package library to be open before use.
 */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_package_path(cpkt_lua_runtime *runtime, const char *path);

/**
 * Prepends an entry to the runtime Lua module search path.
 *
 * Requires the package library to be open before use.
 */
cpkt_lua_runtime_status
cpkt_lua_runtime_prepend_package_path(cpkt_lua_runtime *runtime,
                                      const char *path);

/**
 * Replaces the runtime native-module search path.
 *
 * Requires the package library to be open before use.
 */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_package_cpath(cpkt_lua_runtime *runtime, const char *path);

/**
 * Prepends an entry to the runtime native-module search path.
 *
 * Requires the package library to be open before use.
 */
cpkt_lua_runtime_status
cpkt_lua_runtime_prepend_package_cpath(cpkt_lua_runtime *runtime,
                                       const char *path);

/**
 * Sets a Lua global string value.
 */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_global_string(cpkt_lua_runtime *runtime, const char *name,
                                   const char *value);

/**
 * Sets a Lua global boolean value.
 */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_global_boolean(cpkt_lua_runtime *runtime, const char *name,
                                    int value);

/**
 * Sets a Lua global number value.
 */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_global_number(cpkt_lua_runtime *runtime, const char *name,
                                   double value);

/**
 * Sets a Lua global integer from a C89 long. The accepted range is the host
 * long range; use cpkt_lua_pushinteger() in the full Lua facade for exact
 * two-word 64-bit values on hosts where long is narrower.
 */
cpkt_lua_runtime_status
cpkt_lua_runtime_set_global_integer(cpkt_lua_runtime *runtime, const char *name,
                                    long value);

/**
 * Registers a named C module opener in the runtime preload table.
 *
 * Requires the package library to be open before use. Writes directly to
 * package.preload, bypassing its __newindex metamethod. The runtime retains
 * opener records until destruction, including replaced registrations. Failed
 * registration releases its new record. Loader upvalues are validated against
 * runtime-owned records; foreign or wrong-kind values raise a Lua error.
 */
cpkt_lua_runtime_status
cpkt_lua_runtime_register_c_module(cpkt_lua_runtime *runtime,
                                   const char *module_name,
                                   cpkt_lua_runtime_c_module_open_fn opener);

/**
 * Copies and registers a named Lua source chunk in the runtime preload table.
 * The runtime owns the copy until it is freed; caller storage can be released
 * after this call. Registration writes directly to package.preload, bypassing
 * its __newindex metamethod. A failed registration releases the new copy.
 * The chunk receives all loader arguments, including the module name and
 * loader data supplied by require. Source compilation occurs on first load.
 * The loader validates its upvalue against retained runtime-owned chunks;
 * foreign or wrong-kind values raise a Lua error.
 *
 * Requires the package library to be open before use.
 */
cpkt_lua_runtime_status cpkt_lua_runtime_register_lua_module(
    cpkt_lua_runtime *runtime, const char *module_name,
    const unsigned char *source, size_t source_size, const char *chunk_name);

/**
 * Loads a module for side effects and discards the returned module value.
 */
cpkt_lua_runtime_status cpkt_lua_runtime_require(cpkt_lua_runtime *runtime,
                                                 const char *module_name);

/**
 * Runs a script file synchronously and replaces the Lua global `arg` table.
 *
 * arg[0] is path; arg[1] through arg[argc] copy argv[0] through argv[argc-1].
 * argc must be non-negative; argv may be NULL only when argc is zero. NULL
 * entries become empty strings. CPKT_LUA_RUNTIME_OPEN_LIBS opens the standard
 * libraries before execution. Script return values are discarded; globals and
 * loaded modules remain in this runtime for subsequent calls.
 */
cpkt_lua_runtime_status cpkt_lua_runtime_run_file(cpkt_lua_runtime *runtime,
                                                  const char *path, int argc,
                                                  const char *const *argv,
                                                  int flags);

/**
 * Runs source_size bytes synchronously; source need not be NUL-terminated.
 *
 * source must be non-NULL even for an empty chunk and is borrowed for this
 * call. arg[0] is chunk_name, or an empty string when NULL. Remaining
 * arguments, flags, discarded return values, and persistent state follow
 * cpkt_lua_runtime_run_file().
 */
cpkt_lua_runtime_status cpkt_lua_runtime_run_buffer(
    cpkt_lua_runtime *runtime, const unsigned char *source, size_t source_size,
    const char *chunk_name, int argc, const char *const *argv, int flags);

/**
 * Returns the last facade-owned error message.
 *
 * The returned string is owned by the runtime handle and remains valid until
 * the next facade operation that changes the error state or until the runtime
 * is freed.
 */
const char *cpkt_lua_runtime_error(const cpkt_lua_runtime *runtime);

/**
 * Clears the last facade-owned error message.
 */
void cpkt_lua_runtime_clear_error(cpkt_lua_runtime *runtime);

/**
 * Returns a static status description.
 */
const char *cpkt_lua_runtime_status_string(cpkt_lua_runtime_status status);

#ifdef __cplusplus
}
#endif

#endif
