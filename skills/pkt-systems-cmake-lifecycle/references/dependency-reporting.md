# Project dependency reporting

## When to report

Give a compact dependency summary when dependency state is relevant to the task,
whenever the engineer requests it, and after a successful release. Activating the
skill alone does not require an inventory for unrelated work.
Include the SDK provider, other pkt.systems components, external libraries,
and vendored code that evidence identifies. Distinguish production dependencies
from optional/test inputs and host tools. Reuse that summary during the same
unchanged task rather than repeating a large inventory on every update; refresh
it when pins, selected features, targets, or dependency modes change. A completion
report can say dependencies are unchanged and refer to the earlier summary.

An explicit question such as "what dependencies does this C/CMake project have?"
requests a complete inventory, not an upgrade, configure, or migration. Include
known optional and test dependencies even when disabled, marking their state.
For a successful release, report the exact released inventory in the final
response. Release evidence takes precedence over a pre-build summary.

Keep ordinary summaries short, but do not omit requested categories for brevity.
Say when a category has no dependencies found in the inspected evidence. Do not
claim exhaustive absence when uninspected vendor roots or unresolved dynamic
dependency selection remain.

## Discovery and evidence

Use lightweight read-only inspection. Start with existing dependency manifests,
pins/lock files, top-level and included CMake dependency declarations, presets,
Make/script dependency entrypoints, package metadata and vendor/submodule roots.
Follow actual include/subdirectory and dependency edges; top-level declarations
alone can miss stack components, FetchContent/ExternalProject inputs, embedded
libraries and test packages. Inspect tracked license/notices and source manifests
to identify copied or vendored code with no package-manager declaration.

Prefer these evidence sources for the question being answered:

| Question | Evidence |
| --- | --- |
| What does the current source declare? | Current pins/manifests, CMake/Make/scripts, feature/target conditions and vendor revision records |
| What is configured for a particular build? | That build's resolved dependency manifest, matching contract/cache and imported-target metadata |
| What was actually shipped? | Verified final artifact manifests, installed CMake/pkg-config interfaces, license/notices and payload inventory for that release |

Identify the source revision and target/preset/mode for resolved build evidence.
A stale build cache, unused local SDK, previous release, or different target is
not the current selection. A CMake default may be overridden; report it as a
declared default when no current resolved evidence exists. Keep conflicting
sources visible rather than silently selecting a convenient version.

Report version/revision and pin strength as supported: exact version, commit,
archive checksum, version range, or unpinned/unknown. Never infer an upstream
version from the project's release tag, a facade ABI number, or a URL filename
alone. Record local evidence locations with repository-relative paths or logical
artifact paths. Omit credentials and workstation/cache roots from inventories.

Do not download, probe an archive origin, configure/build, initialize submodules,
install host tools, refresh pins, or launch Actions to fill an inventory gap.
Use existing local evidence and state what is unknown. An already available
archive can be inspected without dependency acquisition; any extraction/report
scratch belongs under `build/`. Do not write transient/date-stamped audit files
into tracked source. Return the requested report directly; update canonical
dependency documentation/manifests only when that work is part of the task.

## Inventory boundaries

For implemented selectable SDKs, report the selected groups, their prerequisite
closure and supplied-but-unused packages separately. Follow
[package-isolation-and-build-reuse.md](package-isolation-and-build-reuse.md) for
composition evidence. A planned split must remain identified as planned.

Use one inventory with distinct categories and dependency relationships:

- **cpkt SDK:** selected bundle version/revision, target/group and
  acquisition mode when known. Identify libraries the project actually consumes
  and their public CMake/pkg-config/facade interfaces. An SDK containing a library
  does not prove the project links it. Expand the SDK's available manifest to
  explain relevant transitive closure; label other bundled libraries as supplied,
  not used. If cpkt is itself the repository being reported, describe it
  as the bundle producer and inventory its upstream components; do not invent
  a dependency on itself. For `cpktdb`/`cpktmisc`, report the owned components and
  their separately pinned core prerequisite, including unresolved target pins.
  Independent provider versions need not match; derive imported versions from
  the validated core manifest. Distinguish implemented source pipelines from
  published assets and successful release evidence.
- **Other pkt.systems components:** every evidenced direct or transitive stack
  dependency, including independently released libraries. For example, `lonejson`
  or `libpslog` belongs here only when a declaration or actual dependency edge
  exists; a README example or nearby checkout is insufficient. Include a test-only
  stack component as test-only, not a shipped/runtime requirement.
- **External libraries and runtimes:** directly acquired upstreams, SDK-provided
  transitive libraries, system requirements and external static/shared consumer
  requirements. Distinguish bundled, statically incorporated, dynamically required,
  externally supplied and build-only inputs. Explain important edges, for example
  a PDF facade's libHaru/libpng/zlib chain, when supported by this project's graph.
- **Vendored/embedded code:** checked-in copies, submodules, fetched sources
  patched/embedded into project outputs, single-header dependencies and upstream
  generated code. Identify upstream origin, available revision/version, patches,
  license/notices and where the code is incorporated. Fetched code is not a
  checked-in vendor tree; generated project-owned facades are not automatically
  third-party code. A copied dependency without a reliable revision is unknown,
  not versioned by the containing project.
- **Development/verification inputs:** optional features, unit/fuzz/benchmark
  dependencies, e2e service images and test-only SDKs. State activation/target
  conditions and image pins when relevant. Separately identify required build
  tools, compiler/sysroot/runtime collections and host programs; do not imply
  that Clang, clangd, clang-format, Valgrind, QEMU or Podman are bundled libraries.
  Native GitHub Actions is a declared verification service/workflow, not a
  software dependency or automatic opt-in; follow [github-actions.md](github-actions.md).

Deduplicate aliases and repeated acquisition paths for the same component while
retaining distinct versions/variants and target differences. A component can
have several roles; show those roles instead of counting it repeatedly as
unrelated dependencies. Keep provider, facade/backend, direct/transitive, and
bundled/external relationships clear. Do not expand an unavailable SDK manifest
into an invented inventory or flatten all supplied libraries into direct deps.

## Report shape and release integration

For a full inventory, use a table with component/provider, version or revision,
role/relationship, selected state and scope/target, and evidence. Add license and
local-patch information when available, especially for redistributed/vendored
inputs. Group common dependencies once and list target/feature differences;
mark unknown versions, unresolved modes, missing upstream provenance and
uninspected closure explicitly. A concise initial summary can group names and
known versions, followed by the principal uncertainty.

During release preparation, reconcile declared pins with the resolved release
inputs and required artifact provenance/notice surfaces. Existing required
manifest/metadata/license failures remain release blockers under
[dependencies.md](dependencies.md) and [packaging.md](packaging.md); reporting
does not weaken those checks or create a second release pipeline.

After the final tagged local build, derive the release inventory from the exact
verified artifact set and source revision. Include bundled upstreams and relevant
external consumer requirements for each shipped target/group, plus vendored
code in source artifacts. State test/build-only dependencies separately; they
must not be presented as shipped merely because release verification used them.
If source and binary artifacts differ in inclusion, report both scopes. Explain
changes since the previous released inventory when available, without claiming
ABI compatibility from versions alone. Do not acquire previous artifacts solely
for this report unless that comparison is separately authorized/required.

Prepare this evidence before publication using existing local gates; after
successful publication include it with the release tag/commit and URL in the
final response. Do not rebuild, edit the released manifests, amend/retag, or
upload an extra audit asset after the final gate just to improve the report.
If release fails, report candidate dependencies as candidate evidence and the
blocker; do not describe them as a successful new release. For a local-only
release, explicitly state that it is not published.
