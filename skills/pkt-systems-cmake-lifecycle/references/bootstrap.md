# Bootstrap Procedure

## Bootstrap Procedure

For a new repository:

1. Extract the declared products, names, dependencies, supported targets and artifact types from the spec. A component may ship only executables; do not invent a library, public headers or SDK delivery.
2. When a public C API is part of the spec, define its handle style, config/value structs, ownership, errors, streaming behavior, examples and compatibility promises before implementation. Otherwise define the declared executable or other product interface.
3. Create the repository layout and `.gitignore`. Ignore generated release/version files such as `/VERSION` so git worktrees do not treat a checked-in version file as the version source.
4. Create `CMakeLists.txt` for the declared products and tests, with project-prefixed options, applicable install rules, dependencies and strict warnings for owned C. Add static/shared libraries, package metadata, examples and optional hooks only when part of that product contract.
5. Create `CMakePresets.json` with the required development presets and release presets for the project's chosen shipment set; do not copy cpkt's entire target matrix into every consumer. Add only relevant optional presets.
6. Create `.clang-format` by dumping the tool's LLVM baseline config with `clang-format -style=llvm -dump-config > .clang-format`, then make any deliberate project style edits in that checked-in file. Do not rely on an implicit clang-format style.
7. Create a simple `Makefile` with the standard surface, including slice, prerelease, hardening, packaging, and optional service/Lua/fuzz targets when those surfaces exist. Keep recipes short; Make is the command surface, not the place for procedural scripting.
8. Declare/select host Bash >= 4.4 for the lifecycle helpers; see [toolchains.md](toolchains.md#host-bash). Follow [tool ownership](../SKILL.md#first-rule-a-simple-native-pipeline): short Make recipes, readable Bash sequencing, CMake build/install rules and CTest tests. Python is limited to generators and fixtures; any other pipeline use needs explicit developer permission. Share native target-tool discovery rather than duplicate it.
9. Add tests first for the applicable public interfaces and lifecycle contracts: behavior, errors, ownership, installation, dependencies, version resolution and declared artifacts. Include source reconstruction, cross-tool discovery and optional e2e/Lua/fuzz/benchmark coverage only for selected surfaces.
10. Implement the code to satisfy the tests.
11. Run affected CTest coverage, then only missing prerelease/package checks. Follow [local-ci.md](local-ci.md#feedback-loop); do not repeat gates already covered.
