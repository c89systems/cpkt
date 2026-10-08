# Declared public interfaces

Apply only when product/API work is authorized. Pipeline changes preserve product
interfaces; the skill is not permission to add a library, restyle handles or
introduce a dependency.

Choose a minimal coherent API with clear ownership, lifetimes, errors and naming.
Document public declarations and provide executable usage examples. Public headers
stand alone under the selected C standard and promised C++ compatibility; private
headers/types stay private. Honor allocator and error cleanup contracts.

Opaque handles and receiver-style shells are product choices. Exposed layout,
method pointers, symbols and callbacks are public ABI where shipped. Follow maturity
and external-support policy for source changes. A required shared-library ABI bump
comes from the latest released ABI and advances exactly one, independently of
repository version or pre-1.0 status. No automatic compatibility shims or format
version bump without a support/coexistence requirement.

Enforce public shared-library exports and public-only facade imports with native
visibility/linker facilities. Test the actual linked and extracted SDK, including
a private-symbol negative consumer when meaningful. Header absence is not export policy.

Declared lonejson/format dependencies own product behavior under
[dependency boundaries](dependencies.md). Generators and fixture JSON can use
native/standard facilities. Keep transport, logging and dependency roots below the API.

Streaming means incremental producer-to-consumer flow with bounded internal buffers.
Do not hide full-message materialization, spooling or reconstruction behind that name.
Preserve upstream callback semantics and disclose backend buffering. If a required
backend cannot stream, report the unmet requirement before implementing a workaround.

Machine actions, credentials, environment helpers and live services require explicit
product configuration. Tests cover relevant denied/error paths without exposing secrets.
Language-specific interop belongs to its facade, with public generic borrowed views
only where needed; do not leak runtime/private layouts into an otherwise generic core.
