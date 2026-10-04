# Dependency upgrades and bundle compatibility

## Routine upgrade boundary

- A normal dependency sweep may upgrade to stable releases only within the
  dependency ABI and consumer compatibility contract of the latest published
  cpkt bundle. Apply this to every shipped dependency, including
  embedded and transitive libraries, within the supported consumer boundary
  defined below.
- Compatible minor and patch upgrades may proceed autonomously after checking
  upstream release notes and compatibility evidence and passing the relevant
  build, test, and package gates. Version numbers alone are not evidence of ABI
  compatibility: minor and patch releases can break consumers, especially for
  pre-1.0 dependencies.
- Compare with the actual dependency versions, ABI metadata, and artifacts in
  the latest published bundle, not merely the previous commit or development
  branch. Record that baseline and the compatibility conclusion in the release
  record; keep transient, date-stamped upgrade audits out of this source tree.
- Check upstream ABI declarations and changes to SONAMEs, Darwin install names
  and compatibility versions, exported symbols, public type layouts, and calling
  conventions where applicable. An unchanged SONAME does not by itself prove
  compatibility. Preserve the upstream ABI identity; never override or disguise
  an ABI bump to make an incompatible library appear compatible.
- Also check source/API requirements, static-link dependencies, CMake and
  pkg-config interfaces, runtime requirements, and documented behavior that
  downstream consumers rely on. Rebuilding this repository successfully is
  necessary but does not prove that existing downstream binaries still work.
- Verify the affected shared and static consumers across the shipped target
  matrix. Use comparison tools and representative consumers built against the
  previous bundle where needed to test existing-binary compatibility. Record
  evidence and any coverage gaps; do not claim compatibility from version
  strings or link smoke tests alone.

## Supported whisper.cpp/ggml consumer boundary

- Downstream whisper.cpp/ggml use is supported through the public C89
  `cpkt_sus` facade. Direct upstream whisper.cpp/ggml API or ABI compatibility
  for external consumers is outside cpkt's support commitment, even
  though their libraries and metadata are shipped in the SDK.
- For those backends, preserve the public facade API/ABI and verify the bundled
  integration, shared/static facade consumers, and speech e2e behavior. Backend
  upgrades may proceed within that boundary when the relevant gates pass;
  a direct upstream ABI comparison is not a shipment gate for external users
  bypassing the facade.
- Preserve upstream ABI metadata and verify the bundled dependency chain. This
  scope does not permit breaking the facade or bundled consumers, and does not
  exempt other dependencies, such as OpenSSL, from their compatibility policy.

## Deliberate compatibility transitions

- Stop the affected dependency upgrade before changing its pin when an ABI
  bump, consumer incompatibility, or unresolved material compatibility question
  is found. Continue independent compatible upgrades where feasible. A generic
  request to update dependencies or prepare a release does not authorize a
  breaking dependency or bundle transition.
- Before requesting explicit approval, prepare a consequence analysis covering:
  - the old and proposed versions, ABI identities, and concrete breaking changes;
  - the reason to transition, including deprecation, end of support, security
    exposure, and whether a supported compatible release remains available;
  - affected bundled and downstream consumers, with evidence that their exact
    versions and enabled features support the proposed dependency;
  - required consumer upgrades, code changes, rebuilds or relinks, toolchain and
    platform changes, and deployment or coexistence requirements;
  - the verification plan, unresolved compatibility risks, rollback limits, and
    proposed bundle version and release communication.
- For example, an OpenSSL 3 to 4 transition requires evidence that the selected
  curl, libssh2, open62541, and other affected consumers support OpenSSL 4 in the
  configurations shipped by this bundle. The existence of an upstream stable
  release is insufficient.
- If the current ABI is deprecated or unsupported, or has no compatible fix for
  a relevant security issue, escalate with the consequence analysis. Do not
  silently cross the ABI boundary or present an unresolved security issue as a
  completed safe upgrade.
- Implement and ship a breaking cpkt bundle only after the maintainer
  explicitly approves that compatibility transition and its consequences.
  Approval of a version number alone does not replace disclosure of a known
  ABI break. Document approved transitions and consumer requirements in the
  release notes, and pass the agreed verification before shipment.
- These dependency constraints apply even while cpkt or an upstream
  dependency is pre-1.0. Routine bundle upgrades must not accidentally impose
  breaking changes on downstream consumers.

## Completion gate

- For changes affecting macOS, push the candidate branch and require the native
  Darwin GitHub Actions workflow to succeed on that exact commit. Inspect failed
  runs, fix actionable issues, and repeat until it succeeds before declaring the
  work complete. A local cross-compile or an earlier successful commit is not a
  substitute for this gate.
- Run `make finalize-slice` after each implementation iteration. It formats
  project-owned C and headers, runs the debug and clangd checks, then asserts
  that formatting is clean.
- After the last edit and before every commit, run `make format-check`. If it
  fails, run `make format`, repeat affected verification, and check again.
