# Lua Facades

Enable Lua facade support when the project ships Lua bindings, a Lua C module, Lua runner behavior, or Lua release artifacts.

This reference governs downstream Lua facade and source-rock distribution. A
bundle producer may intentionally ship upstream Lua runtime/development files
under its declared SDK contract; that does not require it to ship downstream Lua
facades or source rocks. Follow [packaging.md](packaging.md) for that distinction.

Repository contract:

```text
lua/
<project>.rockspec.in
scripts/build_lua_rock.sh
scripts/render_release_rockspec.sh
scripts/stage_lua_rock_sources.sh
```

Build contract:

- `make lua-rock` renders a development rockspec and installs the Lua module into a repo-local tree under `build/luarocks`.
- `make lua-test` runs Lua smoke tests against the repo-local installed rock and a supported Lua interpreter using the selected collection runtime when loading Bootlin-built modules.
- `make lua-env` prints Lua module search paths for the repo-local rock and installed C SDK prefix. It must not export native dependency `LD_LIBRARY_PATH`; use the local interpreter built with the selected runtime for examples.
- Lua 5.5.1 is the preferred and only supported Lua version for pkt.systems projects. Do not support alternate Lua releases or LuaJIT runtimes.
- Host-provided Lua executables may run host-only tooling. Tests loading Bootlin-built C modules must use a local interpreter/embedding executable with the selected collection runtime; see [toolchains.md](toolchains.md#local-execution-with-the-selected-libc).
- It is acceptable to use host-provided LuaRocks for Lua 5.5.1 tooling and source-module builds.
- Bootlin-built C bindings and modules use the selected target Lua headers and libraries, not host development libraries.
- When shipping a C or C++ binary that embeds Lua, prefer the prebuilt Lua 5.5.1 libraries from the cpkt bundle. The upstream Lua 5.5.1 source archive at `https://lua.org/ftp/lua-5.5.1.tar.gz` is the direct-source alternative.
- Missing non-Lua-5.5.1 runtimes are not failures because they are outside the support contract.
- `scripts/build_lua_rock.sh` accepts the LuaRocks build arguments: compiler, flags, shared-library flag, object extension, library extension, and Lua include directory.
- If the Lua module links an installed C SDK, discover it through pkg-config first, then a project-prefixed prefix variable, and fail with an actionable message if neither is available.
- If optional C dependencies can be enabled, probe them at build time and provide force/disable environment variables.
- On Linux, allow undefined symbols in Lua module shared objects only when that is required by Lua module loading.
- Optimize the C-to-Lua boundary deliberately. Prefer APIs that cross the boundary once per batch, stream, buffer, record set, or operation group instead of once per item when throughput matters.
- Lua facade tests and benchmarks should cover both correctness and boundary-crossing shape for hot paths.
- If the Lua facade consumes a released C SDK, test it against the installed SDK layout, not only against the source-tree build.
- For local development, install the just-built C SDK into a repo-local prefix and build the Lua module against that installed layout. This catches C package metadata problems before release.
- Lua package-manager build directories and locks are generated state and belong under `build/` or another ignored generated directory.

Lua module linkage contract:

- Treat a Lua C binding as a facade over the public C library. Compile it against the installed public headers and link it to the public shared-library artifact (`.so` or platform equivalent).
- Do not statically link the public C library into the Lua module, import private implementation targets or source-tree internals, or reproduce private implementation/header declarations in the binding. The Lua facade must know only the public C API it exposes.
- This boundary is especially important for embedded Lua servers: the host binary may already contain the C library and its dependencies through static linkage or loaded shared libraries. A second private or static copy in the Lua module duplicates symbols and state. The public shared-library facade avoids that collision.
- Verify the Lua C binding against an installed public SDK and reject private include paths, static-library linkage, or shadow declarations in its build and export-boundary tests.

Facade contract:

- The Lua facade should mirror the main public workflows: client, agent, session, response/output handles, streaming sinks/readers, tool registries, tool presets, auth/login helpers, and protocol handlers when those exist.
- Omit C-only embedding surfaces that do not map cleanly to Lua, such as custom allocators, `FILE *`, raw C source/sink constructors, C map definitions, or callback types that would force unsafe lifetime rules.
- Lua large-value APIs should expose spooled or callback-backed variants that avoid materializing full payloads when the C API can stream or spool.
- A Lua callback-backed source or sink must pin every native owner it may use
  after invoking a callback; prohibit re-entrant terminal or close operations
  through callback cleanup; and avoid registry-reference cycles with weak
  references or scoped pins. Add an ASan regression that forces collection
  inside read, write, and close callbacks.
- Lua errors should expose structured status, status string, HTTP status when relevant, message, detail, server code, and request id when the C error surface has them.
- Lua tests should cover method-call DX, ownership/finalizer cleanup, spooled readers/writers, and parity for major C workflows.

Lua/C embedder interop contract:

- Do not require every Lua facade to expose C embedder interop. Add it only when C embedders need to borrow Lua-created objects for a real workflow.
- Keep the core C SDK Lua-agnostic. Core public headers must not include `<lua.h>`, forward-declare `lua_State`, expose Lua userdata names, declare `project_lua_*` functions, or require Lua development headers.
- If embedders need a stable borrowed handle to a Lua-created core concept, expose a Lua-free generic view in the core C API only when that view is useful beyond Lua. Use ABI-stamped structs with `size` and `abi_version`, and fail closed on mismatch.
- Put Lua-specific interop in a separate boundary, normally `include/<project>_lua.h` or another clearly named Lua interop header. This header may include `<lua.h>` and declare functions that take `lua_State *`.
- The Lua C module owns userdata validation and conversion from Lua objects to generic core views. Downstream embedders must call the interop functions instead of mirroring private userdata structs.
- Lua-owned schema, route, policy, session, record, or similar objects remain owned by Lua and are valid only on their owning `lua_State`.
- Document that a C consumer retaining a Lua-owned object beyond the current stack frame must keep a Lua registry reference to the userdata, and must not free, resize, mutate, or retain raw record pointers past the documented lifetime.
- Adapter functions should return project status/error values for wrong stack values, wrong userdata, ABI mismatch, ownership mismatch, schema/record mismatch, allocation failure, parse failure, and conversion failure. Expected validation failures must not call Lua argument-error helpers that `longjmp`.
- Interop parse/serialize helpers must preserve true streaming. Reader-backed helpers call the core reader-backed parse API; writer-backed helpers feed the core writer/generator callbacks. Do not convert a Lua table to a full string, accumulate a full HTTP body, or concatenate all chunks and call that streaming.
- If a buffered, materialized, spooled, or file-backed interop helper is useful, name and document it as such.

Release contract:

- `make release-lua-artifacts` writes all Lua release artifacts under `dist/`.
- Render a release rockspec with the exact release version.
- Treat development rockspecs and release rockspecs as different outputs when local development needs repo-local sources. A development rockspec may point at generated local state under `build/`, but no rendered release rockspec, source rock, checksum-listed Lua artifact, or nested source archive may contain a repo-local or `$HOME` source URL.
- Stage a minimal Lua source tree with `LICENSE`, `README.md`, injected source-package `VERSION`, `RELEASE_MANIFEST`, the rockspec template, build/render scripts, Lua sources, C facade sources, docs needed by Lua users, and required public C headers.
- Stage Lua interop headers and implementation fragments required to build the Lua C module, including `<project>_lua.h` when the Lua facade ships C embedder interop.
- Produce a standalone Lua source package under `dist/` named `dist/<project>-lua-<version>.tar.gz`. This package is separate from the C source archive and from per-target C SDK tarballs.
- Build the release `.src.rock` through LuaRocks from a staged Lua source package, not from the live worktree. The source rock must contain the rendered rockspec and the same `<project>-lua-<version>.tar.gz` payload, with the rockspec `source.url` rewritten to a release-local or public source URL and `source.dir` set to the staged root such as `<project>-<version>` when needed.
- Downstream releases ship LuaRocks source artifacts and the source for their C bindings or modules only. Do not ship prebuilt binary Lua modules.
- Keep the rendered release rockspec under `dist/<project>-<version>-1.rockspec` when useful for inspection and checksum coverage.
- Include the standalone Lua source package, rendered release rockspec, and `.src.rock` in the checksum manifest alongside C release artifacts.
- Render release rockspec source URLs as release-appropriate logical or public URLs, such as the final release source archive URL or another deliberately public source location. Do not use absolute `file://` URLs pointing at the repository, `$HOME`, package-manager temporary directories, or staged local source trees.
- Verify rendered release rockspecs, standalone Lua source packages, and source rock contents for private paths, generated state, credentials, absolute local `file://` URLs, and missing source inputs.
- `scripts/validate_luarocks.sh`, when present, must inspect the rendered release rockspec, unpack each `.src.rock`, inspect the rockspec inside it, and expand and scan any nested source archive payloads.
- Expand `.src.rock` artifacts during release privacy verification and scan their nested Lua source package payloads, not only the outer rock file.
- Add a Lua artifact regression test that proves validation fails when a rendered release rockspec, standalone Lua source package, or `.src.rock` contains `file://$HOME`, `file://<repo>`, a package-manager temporary path, or a live-worktree source path.
- Verify Lua release artifact versions agree with the C release version, generated headers, source archive `VERSION`, and checksum manifest.
- When Lua e2e mirrors C behavior, include deterministic local Lua e2e in local smoke gates and live Lua e2e in the explicit live prerelease tier.
- Do not install Lua runtime files, Lua rockspecs, Lua source files, Lua package-manager state, or Lua C binding/facade source files into C binary SDK artifacts.
- Lua C binding/facade code belongs in standalone Lua source packages and source rocks, not in per-target C SDK tarballs, unless the engineer explicitly defines a combined artifact.
- Lua interop headers that include `<lua.h>` belong with the Lua facade/source-rock surface by default. If the engineer deliberately ships them to C embedders outside the Lua source rock, CMake/pkg-config/package metadata must declare the Lua development header requirement explicitly.
- Lua module export-boundary tests must distinguish intentional `project_lua_*` interop exports from accidental leaked `project_*` core implementation symbols.
