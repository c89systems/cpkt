# Dependency acquisition and boundaries

The project spec chooses source dependencies, published SDKs and supported modes.
The lifecycle alone does not select cpkt, host/auto modes or new format libraries.
Pins identify version/revision, target, URL and SHA-256 as applicable. Published
SDK prerequisites use exact verified release assets, not sibling checkouts.

## Shared archives

Resolve `CPKT_DEPENDENCY_CACHE` once: explicit CMake PATH value, environment,
XDG cache, then `$HOME/.cache/cpkt/deps`. Overrides obey the cache ownership
boundary in SKILL.md. Toolchains use their separate cache.
Keep verified archives in `archives/sha256/<digest>/<name>` with per-digest locks.
Names are diagnostic; the digest is identity. Hash before reuse. A verified hit
makes zero acquisition network requests, including probes and metadata refreshes.

On a miss, download to an owned temporary file, verify, then atomically publish.
Use one small acquisition helper with native CMake/Bash, HTTPS verification and
bounded retries. Reject bad/missing hashes and incomplete downloads before extraction.
Never publish a partial archive or silently fall back to host/unpinned content.
Test miss, hit, corruption and request counts against a local fixture origin.

Archives in the declared global cache survive `make clean`. Repo-local
`.cache/` holds disposable extracted/build/install state, owned by component and
target. Native dependencies decide staleness; reprepare only affected owned
components. Borrowed roots fail with their preparation requirement rather than being repaired.
Expected configure-time outputs do not prove a successful producer.
No semantic path-key scheme, contract interpreter or receipt database is required.

## Product and metadata

Respect declared dependency ownership. A declared lonejson or another format
library owns product parsing/serialization/framing covered by its API; do not
create a competing implementation. Missing product capability needs a scoped
decision. Native CMake metadata and standard libraries in permitted source/data
generators and small data fixtures remain available under the Python boundary
in SKILL.md; neither may become a test driver or pipeline controller.

Keep private dependencies below public interfaces. Imported CMake/pkg-config
targets describe complete static link closure, including system/thread/runtime
requirements. Bundled public dependencies retain upstream target names and ship
their headers, metadata and required notices. Consumers should not hard-code
private transitive libraries to make broken metadata pass.

Host/auto modes, when declared, validate complete headers/libraries/metadata and
ABI-sensitive requirements. Partial host installs must not shadow bundled inputs.
Released metadata is relocatable and identifies logical dependencies, not cache roots.

Assess upgrades against the latest published consumer/ABI/runtime commitments;
version numbers alone are not compatibility evidence. Material breaking support
changes need maintainer direction. Internal implementation changes do not create
a new public format version by themselves.
