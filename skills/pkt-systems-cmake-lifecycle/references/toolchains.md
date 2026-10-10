# Selected toolchains and small provisioning helpers

## Contract

Use pinned complete Bootlin collections for Linux compilers, binutils, sysroot and
GNU C++ runtime on a compatible Linux provisioning host. Native macOS uses the
Apple tools below; it does not execute these Linux collections. No host GCC/Clang/libc
fallback. Select only the project's required targets; the supported resolver table
is not a mandatory shipment matrix.

Native macOS uses selected Xcode/Apple tools via xcrun. Linux-host Darwin cross
builds use developer-prepared pinned osxcross and the pinned Linux-host MIG helper.
The developer supplies approved Apple SDK/Xcode input manually; no authenticated
Apple download path, SDK redistribution or deletion of that input is implied.

Host Bash >=4.4 is selected through PATH (Homebrew Bash on macOS).
CMake, Make/Ninja, Git, archive/hash/download tools and required native test tools
are workstation prerequisites. Installation/global account configuration needs
its own authority, never an implicit build recipe. LLVM/Clang stays operator-managed
outside tool caches: it supports osxcross/editor/formatting, not Linux target compilation.
Verify chosen upstream downloads/checksums when provisioning; this skill is not
a complete machine-configuration handbook.

## Linux pins

| Target | Pinned Bootlin collection | Compiler prefix | Sysroot |
| --- | --- | --- | --- |
| `x86_64-linux-gnu` | `x86-64--glibc--stable-2026.08-1` | `x86_64-linux` | `x86_64-buildroot-linux-gnu/sysroot` |
| `x86_64-linux-musl` | `x86-64--musl--stable-2026.08-1` | `x86_64-linux` | `x86_64-buildroot-linux-musl/sysroot` |
| `aarch64-linux-gnu` | `aarch64--glibc--stable-2026.08-1` | `aarch64-linux` | `aarch64-buildroot-linux-gnu/sysroot` |
| `aarch64-linux-musl` | `aarch64--musl--stable-2026.08-1` | `aarch64-linux` | `aarch64-buildroot-linux-musl/sysroot` |
| `armhf-linux-gnu` | `armv7-eabihf--glibc--stable-2026.08-1` | `arm-linux` | `arm-buildroot-linux-gnueabihf/sysroot` |
| `armhf-linux-musl` | `armv7-eabihf--musl--stable-2026.08-1` | `arm-linux` | `arm-buildroot-linux-musleabihf/sysroot` |

Pins and checksums are authoritative in scripts/cpkt-toolchains.sh. Updating a
collection changes its archive, digest, prefix and sysroot coherently and verifies
real use/compatibility. Do not select a newer compiler merely because it is installed.

## Cache and helper location

Toolchain cache is CPKT_TOOLCHAIN_CACHE, XDG cache or $HOME/.cache/cpkt/toolchains.
Dependency archives have a separate cache under [dependencies](dependencies.md).
Cache overrides obey the ownership boundary in SKILL.md; use stable absolute paths.
Readiness reports are simple line-oriented fields;
control characters/newlines in configured paths are outside that interface.
Ordinary spaces and argv boundaries remain supported.

Keep archives verified and roots immutable after successful preparation.
Per-collection flock locks serialize explicit provisioning; readiness is rechecked
under the lock, staging is owned, and publication occurs only after success.
A verified archive hit makes no acquisition requests, even if an asset name changes.
Full `make clean` preserves the declared global cache. Missing prerequisites
and observed corruption fail; reprepare through the owner. Do not add per-file
cache attestation.

Use the activated skill's scripts directory or the declared exact vendored copy.
Keep require-host-bash.sh, cpkt-archive-cache.sh, cpkt-toolchains.sh, cpkt-aflpp.sh and
cpkt-afl-runtime.sh together. Resolve lifecycle_scripts_dir from that actual location.
A downstream checkout need not contain skills/ or scripts/ copies.
Local helper/cache paths never enter installed metadata.

The helpers use foreground execution. The invoking terminal/job runner supervises
and cancels the whole operation/process group under SKILL.md. Sending a signal
only to a wrapper PID is not their cancellation interface. Catchable failure cleans
owned staging; force-killed residue is unverified and never accepted as output.
Do not add supervisors, descendant enumerators or recovery daemons to these helpers.

## Direct commands

~~~bash
"$lifecycle_scripts_dir/cpkt-toolchains.sh" ensure "$selected_target" || exit 1
description=$("$lifecycle_scripts_dir/cpkt-toolchains.sh" discover "$selected_target") || exit 1
resolved_env=$("$lifecycle_scripts_dir/cpkt-toolchains.sh" env "$selected_target") || exit 1
eval "$resolved_env"
~~~

Only ensure provisions. discover/env are read-only and must refuse unavailable
inputs before exports/configure. Check status before eval; eval "$(resolver env)"
can hide failure. Selected/borrowed operations do not implicitly ensure tools.

The six Linux pins support GNU/musl x86_64, aarch64 and armhf. Darwin discovery
requires complete target tools and the pinned MIG collection. OSXCROSS_ROOT and
CPKT_OSXCROSS_HOST choose the approved local collection/prefix, not arbitrary host
tools. Resolve the real osxcross linker explicitly with --ld-path; no fabricated aliases.
Prove a target Mach-O smoke executable before declaring Darwin preparation usable.

## CMake configuration

Provision at the authorized outer entrypoint, then discover before project()
through a CMake toolchain file. Configure and try_compile never acquire tools.
Set compilers, binutils, sysroot and find-root behavior coherently. Pass the helper
location through CPKT_LIFECYCLE_SCRIPTS_DIR, including try_compile propagation.
This Linux example uses CMake's graph rather than an alternate configuration engine:

```cmake
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CPKT_LIFECYCLE_SCRIPTS_DIR)
function(project_configure_bootlin_toolchain target_id)
  if(NOT DEFINED CPKT_LIFECYCLE_SCRIPTS_DIR OR
      NOT EXISTS "${CPKT_LIFECYCLE_SCRIPTS_DIR}/cpkt-toolchains.sh")
    message(FATAL_ERROR "Set CPKT_LIFECYCLE_SCRIPTS_DIR to the resolved lifecycle scripts directory")
  endif()
  set(resolver "${CPKT_LIFECYCLE_SCRIPTS_DIR}/cpkt-toolchains.sh")
  execute_process(COMMAND "${resolver}" discover "${target_id}"
    RESULT_VARIABLE result OUTPUT_VARIABLE description ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Unable to inspect the pinned Bootlin toolchain: ${error}")
  endif()
  if(NOT description MATCHES "(^|[\r\n])status=ready([\r\n]|$)")
    message(FATAL_ERROR "Missing pinned Bootlin collection; prepare explicitly with: ${resolver} ensure ${target_id}")
  endif()
  foreach(key cc cxx ld ar ranlib strip nm objcopy objdump addr2line readelf sysroot root)
    string(REGEX MATCH "(^|[\r\n])${key}=([^\r\n]+)" match "${description}")
    if(NOT match)
      message(FATAL_ERROR "Bootlin resolver did not report ${key} for ${target_id}")
    endif()
    set(bootlin_${key} "${CMAKE_MATCH_2}")
  endforeach()
  set(CMAKE_C_COMPILER "${bootlin_cc}" CACHE FILEPATH "" FORCE)
  set(CMAKE_CXX_COMPILER "${bootlin_cxx}" CACHE FILEPATH "" FORCE)
  set(CMAKE_LINKER "${bootlin_ld}" CACHE FILEPATH "" FORCE)
  set(CMAKE_AR "${bootlin_ar}" CACHE FILEPATH "" FORCE)
  set(CMAKE_RANLIB "${bootlin_ranlib}" CACHE FILEPATH "" FORCE)
  set(CMAKE_STRIP "${bootlin_strip}" CACHE FILEPATH "" FORCE)
  set(CMAKE_NM "${bootlin_nm}" CACHE FILEPATH "" FORCE)
  set(CMAKE_OBJCOPY "${bootlin_objcopy}" CACHE FILEPATH "" FORCE)
  set(CMAKE_OBJDUMP "${bootlin_objdump}" CACHE FILEPATH "" FORCE)
  set(CMAKE_ADDR2LINE "${bootlin_addr2line}" CACHE FILEPATH "" FORCE)
  set(CMAKE_READELF "${bootlin_readelf}" CACHE FILEPATH "" FORCE)
  set(CMAKE_SYSROOT "${bootlin_sysroot}" CACHE PATH "" FORCE)
  set(CMAKE_FIND_ROOT_PATH "${bootlin_sysroot}" "${bootlin_root}" CACHE STRING "" FORCE)
  set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER CACHE STRING "" FORCE)
  set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY CACHE STRING "" FORCE)
  set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY CACHE STRING "" FORCE)
  set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY CACHE STRING "" FORCE)
endfunction()
```


For a cross target, also set CMAKE_SYSTEM_NAME/PROCESSOR and static-library
try_compile before applying the collection. Native Darwin uses its own Apple setup.

Set `target_sdk_prefixes` to the project's verified target SDK installations.
After selecting the compiler collection, append them to both search lists.
Package lookup remains restricted to target roots:

```cmake
list(APPEND CMAKE_FIND_ROOT_PATH ${target_sdk_prefixes})
list(APPEND CMAKE_PREFIX_PATH ${target_sdk_prefixes})
```

Each SDK retains its own installation prefix. Validate discovered prerequisite
identity and target; do not enable host fallback to reach an SDK.

Apply -std=c89 and strict warning/pedantic flags to owned C89 targets explicitly;
keep this off deliberately newer upstream/Lua implementations. Generated large
text assets come from canonical tracked bytes through a declared CMake generator,
with deterministic contiguous C arrays and a defined length/NUL contract.
Do not use runtime joins to evade C89 source-line limits.

## Runtime and instrumentation

Local non-shipped Linux executables use the selected ELF interpreter and private
runtime paths, including indirect libraries. Their children do likewise. Keep
these flags off shipped binaries, installed metadata and example source.
Actual loaded objects/consumers establish runtime proof; a path alone does not.
Cross runtime checks use the declared runner, never a native loader for a foreign ISA.
A cross target without runtime opt-in reports compile/link/inspection proof.

Native x86_64 Linux Valgrind runs selected-collection executables; it is not MSan.
AFL++ uses the pinned cached release/GCC plugin and matching Bootlin compiler,
backends, loader and C++ runtime. Cold and cached readiness check relevant inputs.
No Valgrind/AFL through QEMU or on native macOS. Do not export target library paths
to host Shell/Git/CMake processes; no arbitrary-exec interception to select libc.

For C++ facades, Linux static metadata ships/links matching libstdc++.a and libgcc.a
without merging them into facade archives. Darwin uses its declared libc++ closure.
Prove installed consumer links and runtime where selected, keeping workstation
paths out of the SDK.

## Verification

Configure the skill's small CTest graph in owned, ignored build scratch (the caller's
precondition, not a Git-policy check in this graph) and select resolver
tests after helper changes. Fixtures use synthetic local inputs and make no real
downloads/builds. They verify supported behavior; they do not simulate a hostile
workstation or add policy word-matching tests.

~~~bash
cmake -S "$skill_tests_dir" -B "$project_root/build/skill-validation" -DSKILL_WORKSPACE_ROOT="$project_root"
ctest --test-dir "$project_root/build/skill-validation" -L resolver --stop-on-failure --output-on-failure --no-tests=error
~~~

Resolver fixtures apply on Linux; AFL++ requires native x86_64 Linux.
Changed pins or output-affecting compiler/tool recipes additionally need relevant
real configure/build/instrumentation proof. Ordinary documentation or validation
changes do not mandate a complete component matrix.
