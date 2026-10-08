# Bootstrap from the project spec

1. Identify declared products/interfaces, dependencies, targets and artifacts.
   An executable-only project does not acquire a public C library or SDK.
2. Define the selected API/CLI and its observable tests before implementation.
3. Add only needed source/layout, presets, install metadata and optional surfaces.
   Configure strict warnings for owned code and a checked-in clang-format style.
4. Use short Make recipes, direct Bash sequencing, CMake dependencies and CTest.
   Select tools under [toolchain policy](toolchains.md); generators/fixtures follow
   the Python exception in SKILL.md.
5. Prove affected behavior, installation and packaging through focused checks,
   then complete missing selected coverage. No duplicate rehearsal/release graph.
