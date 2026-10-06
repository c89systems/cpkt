# Bootstrap Procedure

## Bootstrap Procedure

For a new repository:

1. Extract product name, C library name, namespace, binary name, public headers, examples, dependencies, Lua needs, e2e services, target matrix, and artifact types from the spec.
2. Define the public C API shape before implementation: handle style, receiver shells or opaque handles, transparent config/value structs, required free functions, ownership rules, error surface, streaming/spooling names, examples, and compatibility promises.
3. Create the repository layout and `.gitignore`. Ignore generated release/version files such as `/VERSION` so git worktrees do not treat a checked-in version file as the version source.
4. Create `CMakeLists.txt` with project-prefixed options, static/shared library targets, install rules, package config generation, tests, examples, fuzz/bench/e2e hooks as needed, dependency root variables, and strict warning policy for project-owned C.
5. Create `CMakePresets.json` with the required development presets and release presets for the project's chosen shipment set; do not copy cpkt's entire target matrix into every consumer. Add only relevant optional presets.
6. Create `.clang-format` by dumping the tool's LLVM baseline config with `clang-format -style=llvm -dump-config > .clang-format`, then make any deliberate project style edits in that checked-in file. Do not rely on an implicit clang-format style.
7. Create a simple `Makefile` with the standard surface, including slice, prerelease, hardening, packaging, and optional service/Lua/fuzz targets when those surfaces exist. Keep recipes short; Make is the command surface, not the place for procedural scripting.
8. For the modern helper collection, declare host Bash >= 4.4, add a lightweight early startup check, document package installation and PATH selection, and provision/select Bash in each native CI job before lifecycle use (see [toolchains.md](toolchains.md#host-bash)). Put procedural lifecycle work in readable Bash scripts, and keep build rules in CMake under [operability.md](operability.md#tool-ownership-and-simplicity). Introduce Python3 only for a focused task with a documented technical justification; do not create a Python lifecycle dispatcher or build scheduler. When cross-target packaging, Darwin artifacts, or runtime-path verification are in scope, include shared target-tool discovery such as `scripts/discover_target_tools.sh` instead of duplicating lookup in package scripts.
9. Add tests first for public API, preferred API style, error behavior, allocator/ownership behavior, install-tree consumers, dependency modes, version resolution, packaging, source archive smoke when sources are shipped, target-tool discovery when cross artifacts are shipped, and selected e2e/Lua/fuzz/benchmark surfaces.
10. Implement the code to satisfy the tests.
11. Run the narrow gates, then the prerelease tier, then release-matrix or `make package-verify`.
