# Dependency reporting

Report when requested, relevant to the task, and after release. Use existing
read-only source declarations, pins, vendor records, configured metadata and
verified artifact manifests. Do not download/build/upgrade just to fill a report.

Separate declared defaults, current resolved inputs and actually shipped payload.
Identify revision and target/mode for build evidence; stale caches are not current
selection. Unknown versions or uninspected closure stay explicit.

A requested inventory includes direct/transitive stack components, external
libraries/runtimes, vendored code, optional/test dependencies and host tools.
Distinguish supplied-but-unused SDK payload from linked dependencies, bundled
content from external static/runtime requirements, and test tools from shipments.
Do not infer a cpkt dependency or a library version from the containing project's tag.

Use a concise table of identity, version/pin strength, role, selected scope and
evidence. Include licenses and local patches for redistributed code.
Deduplicate repeated acquisition paths without hiding target/variant differences.

Before publication reconcile the inventory with final artifacts and notices.
After release report the exact verified shipment and external consumer requirements,
tag/commit and URL. Never rebuild, retag or add an audit artifact to improve prose.
Failed/unpublished candidates must remain described as candidates.
