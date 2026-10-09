# Verify the declared shipment

The owning spec selects artifacts and targets. Single-package defaults are
`<project>-<version>-<target>.tar.gz`, optional source `<project>-<version>.tar.gz`
and `<project>-<version>-CHECKSUMS`. Composable providers use their declared inventory.

Stage through CMake install. Include only owned public payload, needed consumers'
metadata/docs and licenses/notices. No private headers, generated build/cache state,
credentials, VCS data, developer service/fuzz state or workstation paths.
Intentional bundled dependencies retain their public interfaces and provenance.

Use deterministic archive tools: stable ordering, timestamps, ownership and gzip
metadata; preserve intentional executable/file modes. Test the actual package
entrypoint with unchanged content and changed staging timestamps/umask where needed.

The complete SHA-256 manifest is the upload inventory. It lists every intended
payload, each must exist/verify, and the manifest itself is uploaded. Reject
stale/unlisted release-looking artifacts. No unrestricted dist glob for uploads.
Binary rehearsals use an explicit binary-scope manifest under build, not a partial
replacement for the final manifest. Warm operations preserve unrelated identified
artifacts. Selected package work stays in owned staging and cannot claim full release.

## Final-byte checks

Extract final archives in owned build scratch and verify exact layout/inventory,
architecture, versions, licenses, dependency metadata, public exports/imports and
relocatable CMake/pkg-config interfaces. Build real static/shared installed consumers
and shipped examples as applicable; metadata must supply their full static closure.
Relocate the SDK and repeat relevant consumer proof. Keep test-only flags private.

Execute consumers where the declared host/runner supports it. Report deferred cases;
required runtime gates cannot be replaced with build proof. A packaged Darwin
artifact still needs target-correct Mach-O inspection even on a Linux host.

Scan shipped text/binaries and nested release-format archives for private/local
paths, credentials, instrumentation and unintended payload. Names/headers alone
are insufficient. Negative fixtures should demonstrate important failure gates,
not create a general recursive file-format interpreter.

ELF runtime paths are absent or $ORIGIN-relative. Darwin project dylib identities
are ABI-correct @rpath names; dependencies are relative or system /usr/lib and
/System/Library paths. Runtime search uses @loader_path/@executable_path as needed.
No absolute local/cache/toolchain paths. Inspect extracted bytes with target tools.

Prefer correct link/install metadata to post-package mutation. Darwin strip and
install_name_tool can invalidate signatures: mutate before signing, verify afterward,
or avoid mutation when a signer is unavailable. Linux/osxcross does not require
Apple codesign; native macOS verifies signatures when part of its selected flow.

## Source and optional formats

Stage source from an explicit manifest, including all code/build inputs, generators,
tests/data needed for reconstruction, license and injected VERSION/RELEASE_MANIFEST.
Exclude generated/private trees. Derive Git payload from owned tracked source;
an extracted archive uses its own manifest/version even beneath another checkout.

When source ships, prove extraction/build/tests from clean compiled state and
version agreement with installed metadata. Integrate it as the native Release lane
where useful to avoid a second equivalent producer. Binary preparation does not
automatically reconstruct source. Expose source smoke explicitly.

Single-header products have generation/version and compile smoke proof. Lua
facade/source-rock products follow [Lua](lua.md). Vendored patches have recorded
origins, license/notices, clean application and relevant build tests; do not invent
an upgrade command suite for undeclared maintenance workflows.
