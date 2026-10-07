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
8. Declare/select host Bash >= 4.4 for the lifecycle helpers; see [toolchains.md](toolchains.md#host-bash). Follow [tool ownership](../SKILL.md#first-rule-a-simple-native-pipeline): short Make recipes, readable Bash sequencing, CMake build/install rules and CTest tests. Python is limited to generators and fixtures; any other pipeline use needs explicit developer permission. Share native target-tool discovery rather than duplicate it.
9. Add tests first for public API, preferred API style, error behavior, allocator/ownership behavior, install-tree consumers, dependency modes, version resolution, packaging, source archive smoke when sources are shipped, target-tool discovery when cross artifacts are shipped, and selected e2e/Lua/fuzz/benchmark surfaces.
10. Implement the code to satisfy the tests.
11. Run affected CTest coverage, then only missing prerelease/package checks. Follow [local-ci.md](local-ci.md#feedback-loop); do not repeat gates already covered.
