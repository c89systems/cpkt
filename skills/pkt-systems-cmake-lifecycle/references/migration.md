# Migrate within the authorized scope

Inventory existing behavior, interfaces, dependencies, tests and deliveries.
A pipeline migration preserves product behavior and does not authorize API redesign.

Replace competing controllers with native Make/Bash/CMake/CTest and one graph.
Remove dead paths in the same cutover; do not port a large framework wholesale.
Keep compatibility only for a declared support commitment. Respect shared ABI and
published artifact/dependency promises independently of release maturity.

Preserve useful tests, data files, licenses, generators, suppression policies and
seed corpora. Add missing behavior verification before deleting old paths. Prove
input closure and actual install/source delivery; syntax checks alone are insufficient.
A concise migration ledger is useful only while a nontrivial migration needs it.

An explicitly requested repository split inventories every owning surface before
moving files. Preserve public headers/ABI and each destination's identity/license.
Do not operate on siblings through one provider's all/clean/release.
Optional providers acquire their exact published prerequisites; unresolved pins
stay unresolved until verified assets exist. Synthetic fixtures are not releases.
Release providers independently in dependency order, and leave archival to the maintainer.

Update authoritative skill sources for working contracts. Installing updated skills
is a separate requested operation; this guide does not authorize cross-repo edits.
