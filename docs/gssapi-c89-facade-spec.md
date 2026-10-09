# GSSAPI C89 facade

`cpkt/gssapi.h` is the supported C89 interface to bundled MIT Kerberos
GSSAPI. It intentionally keeps provider headers, fixed-width integer typedefs,
and C++ linkage declarations outside the consumer boundary.

CMake consumers can locate the SDK using only `CpktGssapi_DIR` or
`Kerberos5_DIR`. Each package resolves OpenSSL from its adjacent SDK directory;
no separate `OpenSSL_DIR` or host OpenSSL installation is needed. Package
verification tests this discovery independently, including after relocation and
with a competing OpenSSL package in the search path.

The facade covers mechanism and attribute discovery, OID conversion, names and
name attributes, credential acquisition and storage, initiator and acceptor
contexts, status rendering, buffer sets, MIC, AEAD, PRF, token handling, and
IOV operations. The Kerberos extension adds ticket flags, credential/cache
operations, PAC mapping, and lucid context export. Narrow Kerberos context,
cache, keytab, principal, and replay-cache handles are provided where those
extensions require them; they are not a general Kerberos API.

`cpkt_gss_set_trace_callback()` registers a process-wide destination for
formatted MIT Kerberos trace records from subsequently created Kerberos GSS
contexts, including SPNEGO/NegoEx. Register before concurrent GSS operations.
The callback receives one borrowed record per call and its caller-owned context
must remain valid while registered. Passing null stops delivery; new contexts
then keep native `KRB5_TRACE` file behavior. Existing contexts retain the
bridge but stop delivering after registration is cleared. This is a trace hook,
not a GSS major/minor status replacement: callers should continue to inspect
operation results with the status accessors.

Statuses, flags, QOP values, and lifetimes are exact unsigned 32-bit values
carried in `unsigned long`. Calls reject values that exceed the native range.
Lucid context sequence numbers use separate high and low 32-bit words, so
their full 64-bit values survive on 32-bit C89 targets. This representation is
independent of the native structure layout.

All handles are opaque. Results returned through a `cpkt_gss_buffer` are
provider-owned and must be passed to `cpkt_gss_release_buffer`. OIDs and OID
sets returned by creation or conversion operations must use their corresponding
release operation. Standard name-type OIDs and OID-set members are borrowed.
Mechanism-set discovery and creation, OID text parsing, name import/duplication/
canonicalization, mutable OID-set updates, and context negotiation require
their handle output or in/out pointer. A null pointer returns
`CPKT_GSS_S_CALL_BAD_STRUCTURE` with minor `EINVAL` before provider allocation;
the minor-status pointer itself remains optional. Inquiry outputs documented
as optional retain their existing behavior, including release of omitted
provider-owned results.
Buffer-set members and the byte views returned by `cpkt_gss_oid_view()` are
borrowed. Release a buffer set once, after all member views are finished.
`cpkt_gss_local_login_attribute()` returns borrowed provider-static bytes;
do not release them. `cpkt_gss_oid_from_bytes()` copies caller DER bytes into
an owned descriptor, which is released with `cpkt_gss_release_oid()`.
IOV entries are copied into native descriptors for a call and copied back,
including partial outputs and allocation flags on failure. Release entries
marked `CPKT_GSS_IOV_ALLOCATED` with `cpkt_gss_release_iov()` after use.
Lucid key bytes are borrowed from the owned lucid context; release the context
with `cpkt_gss_krb5_free_lucid_context()`.
The destructor clears the owned view only on success. Provider errors retain
the view and borrowed key data for a retry.

The ODBC session-key inquiry OID is defined by the staged static Kerberos
library but not exported by its shared library. The facade carries a
provider-byte-equivalent descriptor for that one OID. Native-peer parity is
checked against the static provider, and the shared facade has no unresolved
import for it. The public `gss_export_name_object`,
`gss_import_name_object`, and `gss_initialize` declarations have no linked
implementation in the staged provider. The three `gssspi_*` metadata/query
declarations are static-only mechanism-glue interfaces. No facade call
pretends to implement these unavailable functions. The installed
`gssapi_ext.h` describes the three as SPIs implemented by NegoEx mechanisms,
and the staged `mechglue/g_negoex.c` dispatch is called only by SPNEGO. They
are mechanism-provider entry points, not generic application dispatch calls;
the shared provider does not export them. The static-only
`krb5_gss_oid_array` is provider-internal data, not a consumer OID handle.

`tests/contracts/auth_native_api.json` freezes compiler-enabled declarations,
record fields, typedefs, constants, and public variables for all seven SDK
targets. The adjacent auth binding manifests identify every callable facade
binding, mapped field, numeric value, and intentionally unavailable symbol.
`auth_native_api_contract`, `auth_facade_binding_contract`, and
`auth_facade_signature_contract` compile these
contracts with the configured target toolchain. The auth export policy checks
the exact public symbol catalog and provider imports.

PostgreSQL uses the bundled provider internally for GSS authentication. Its
separate `cpkt/postgres.h` boundary does not expose provider-specific GSS
objects.
