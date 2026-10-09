# Selected packages and producer reuse

Apply only to implemented selection/composition surfaces. Splitting packages is
not mandatory. CMake declares dependencies, producers, install outputs and test
ownership; package metadata describes payload and must not become another graph.

Validate selectors before mutation. Selected operations touch only owned state;
they neither build, repair, test nor clean borrowed prerequisites or siblings.
Missing/stale prerequisites fail with a preparation requirement. Unqualified all
means this provider, not a family-wide operation.

One producer serves an effective source/toolchain/flags/dependency input set.
Share matching upstream output between consumers; distinct instrumentation keeps
distinct output. A test edit preserves an unchanged library. Reuse valid checks
under [local CI](local-ci.md), without per-case receipts or a generic identity engine.

Installed combinations have unique payload owners, declared imports and no cycles
or collisions. Where order independence is promised, test both extraction orders.
Pin prerequisite version, target, archive digest and public package identity.
Validate metadata, licenses, consumer links and actual delivered bytes.

Selected staging/checksums live in owned `build/` namespaces and cannot authorize
complete-release uploads or replace `dist/` proof. Final release rejects partial
selectors and covers its declared complete set. Cold release shares equivalent
producers/checks but cannot borrow earlier rehearsal success.

Use focused CTest fixtures for no-op reuse, invalidation, wrong/missing inputs,
ownership and consumer behavior. Measure suspected duplicate work with existing
logs; do not add a scheduler or performance instrumentation platform.
