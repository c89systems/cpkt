# Native lifecycle architecture

Make exposes public names and short recipes. `scripts/lifecycle.sh` validates
selectors and routes to readable Bash workflows. `make help` is the command
index. `scripts/build.sh configure|build|test|deps|path|preflight` is the native
build surface. `scripts/operation.sh --group <owner|all> -- <command>` supplies
repository ownership; it never widens scope to a sibling checkout.

CMake evaluates presets, selects tools, owns ExternalProject dependency edges,
component contracts, incremental builds, CTest registration and required
coverage. `CpktABIVersions.cmake` owns facade ABI declarations. Bash sequences
producer preparation and consumer configuration; it retains cached job limits,
with Linux maximum 8 and native Darwin maximum 2. Hardening borrows verified
ordinary Debug output. Missing/stale borrowed inputs fail with preparation
commands. Ordinary work does not clean shared archives or toolchain caches.
Before accepting a saved contract, native CMake checks its recorded recipe,
file, inventory and transitive dependency inputs against current source bytes.
This read-only check also protects hardening that borrows ordinary Debug output;
it never configures a producer or rebuilds a dependency.
Verified warm ExternalProject nodes retain their dependency edges and validate
original receipts on every native build. Changed launcher/stamp scripts never
replay a compiled install. Recipe/input or output corruption selects the full
native producer path; the native adapter's actual bytes are part of that contract.

CMake install rules stage owned SDK payload, public generated headers, discovery
metadata, example sources and complete licenses. `scripts/archive.sh` uses GNU
tar and gzip with fixed timestamps/order/ownership and compression level 6.
Manifests retain content, modes, symlinks, versions, source hashes, facade features,
ABI/loader identities and exact optional-core requirements. Optional providers
use their own archive roots and versions and require a published checksum pin;
empty target pins remain intentional until publication.

Bash owns flock, stable lock inodes, nested operations, process groups, bounded
signal cleanup and command diagnostics. Persistent service execution closes the issued descriptor numbers, including recovered handles, and clears operation capabilities before exec. `scripts/package-command.sh` applies
that process policy to validator-owned external tool probes too. The Python
output adapter forwards signals to this Bash owner and reads its output; it does
not manage groups or schedule builds. Fixture reuse requires the same live run,
exact input bytes/environment, command argv and output identities. Completed
readiness requires every native required CTest case exactly once in successful
JUnit; filtered or failed runs cannot publish full readiness.
Readiness binds the current CMake graph and preset source bytes as well as the
configured outputs. Release verification aliases always select complete release
scope, including source artifacts and reconstruction evidence.

Source reconstruction uses `scripts/source-reconstruct.sh` in an independently
locked extracted source root with the parent's actual shared cache, generator and
lower job limit. Source and native Darwin proofs record actual executions, never
invented readiness. Release remains a separate final workflow; implementation
fixtures must use throwaway repositories for Git ref interruption/recovery.

## Python exceptions

The following are specialist tasks, not build controllers. Each helper has a
specific reason; JSON alone is insufficient. No Python preset interpreter,
lifecycle dispatcher or dependency scheduler is retained.

- **POSIX descriptor authentication** — `scripts/cpkt_lock.py`: Bash flock handles are intentionally closed by CMake/CTest. fstat, pread, access-mode inspection and authenticated SCM_RIGHTS transfer reissue the exact live descriptions, including long socket paths; Bash owns locking, process groups, waiting and cleanup.

- **SDK/archive/binary structure** — `scripts/validate-sdk.py`, `scripts/cpkt_packages.py`, `scripts/cpkt_archive_assert.py`, `scripts/cpkt_archive_extract.py`, `scripts/cpkt_package_manifest.py`, `scripts/cpkt_verify_manifest.py`, `scripts/cpkt_darwin.py`: Canonical manifests, duplicate-key rejection, safe archive member/symlink preflight, ELF/Mach-O load identities, exact composition requirements, archive privacy and deterministic ZIP modes need one structured validation implementation. These helpers validate bytes and evidence; native install rules and GNU tar own SDK staging and tar creation.

- **Content and test evidence** — `scripts/cpkt_inventory.py`, `scripts/cpkt_receipts.py`, `scripts/cpkt_receipt_cli.py`, `scripts/cpkt_configure_guard.py`, `scripts/cpkt_build_evidence.py`, `scripts/cpkt_memcheck_evidence.py`, `scripts/cpkt_fixture_identity.py`, `scripts/cpkt_helper_proof.py`, `scripts/cpkt_source_proof.py`: Content/mode/symlink closures, canonical atomic receipts, CTest JSON and JUnit/Memcheck XML are compared structurally. These validators neither select build order nor interpret presets. CMake computes component contracts and required coverage; Bash executes commands. The inline checks in `scripts/source-archive-verify.sh` validate the reconstructed inventory and canonical source manifest against extracted files before building; they perform no lifecycle dispatch.

- **Installed consumer specification** — `scripts/cpkt_sdk_consumer.py`, `scripts/cpkt_sdk_examples.py`: Generate independent downstream CMake graphs and validate exact static/shared/PIC closures, pkg-config argv and loader evidence against manifest geometry. CMake owns compilation and target dependencies. Python collects structured executable/ABI evidence; package-command.sh owns process groups and cancellation.

- **Native configuration adapters** — `scripts/cpkt_presets.py`, `scripts/configured_build.py`, `scripts/cpkt_darwin_tools.py`: Specialist validators need configured paths and tools. These small adapters bind a typed configured tool map to the ELF/Mach-O and consumer validators, check cold facade ABI defaults through native CMake, and reject host fallback or mismatched target tools; no preset inheritance, scheduling or lifecycle dispatch exists here.

- **Editor validation and compiler dependencies** — `scripts/cpkt_clangd_check.py` and the inline validator in `scripts/verify-clangd-surface.sh`: Parse compiler Make dependency escaping and JSON compile commands to bind the actual transitive header/tool byte closure, then atomically publish the editor database and same-run proof. The inline validator parses C declarations, definitions and adjacent Doxygen comments; clangd checks use the actual compiler input closure. Bash performs editor commands.

- **API contracts** — `scripts/auth_api_contract.py`, `scripts/auth_sasl_property_contract.py`, `scripts/auth_facade_bindings.py`, `scripts/auth_facade_signatures.py`, `scripts/db_api_inventory.py`: Compare parsed C declarations, facade mappings, signature/ownership rules and native inventories. Syntax and binding structure cannot be checked reliably by shell token substitution.

- **Remote and Git evidence** — `scripts/cpkt_github_handoff.py`, `scripts/cpkt_reserved_tag.py`: Validate authenticated GitHub JSON, official redirect/credential boundaries, checksum cache identities or owned ref recovery records. Bash version-contract.sh performs Git ref creation/deletion. This refactor does not invoke remote publication or tag mutation.

- **Selected executable preflight** — `scripts/run-selected-tests.py`: Read CTest JSON and reject absent/non-executable command arrays before any selected case executes; shell text parsing cannot safely preserve arbitrary JSON argv. CTest owns test execution.

The `tools/generate_cmocka_c89.py`, `tools/generate_libssh2_api_inventory.py`, `tools/generate_libssh2_c89_facade.py`, `tools/generate_lua_api_inventory.py`, `tools/generate_lua_c89_facade.py`, `tools/generate_mqttc_api_inventory.py`, `tools/generate_mqttc_c89_facade.py`, `tools/generate_nghttp2_api_inventory.py`, `tools/generate_nghttp2_c89_facade.py`, `tools/generate_openssl_api_inventory.py` helpers parse C headers and symbol inventories and emit C89 facade sources or ABI inventories. Each generator is owned by its CMake custom command, with explicit input/output edges. They require structural declaration/type/symbol processing rather than shell substitution. Python tests construct isolated adversarial archive, metadata, compiler and POSIX fixtures and assert observable behavior; they do not drive production builds. Optional `cpkt_core.py` validates exact published pin schemas, SDK manifests, archive identities and imported component records; Bash/CMake own acquisition and extraction sequencing.
