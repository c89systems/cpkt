# OpenSSL C89 facade surface contract

OpenSSL 3.6.5 is not a direct C89 dependency. Its installed headers expose
64-bit scalar parameters, results, and legacy state records; strict C89 has no
portable scalar representation for those values. `cpkt_openssl` must therefore
adapt the complete configured OpenSSL public API, not a convenient TLS subset.

The bundled OpenSSL carries a local teardown patch that releases its internal
master TLS key after global cleanup. Private statically linked plugins can unload
without exhausting pthread key slots. Stop all threads using that private OpenSSL
instance before unloading it; do not call global cleanup on a host's shared
OpenSSL instance. GNU/Linux CTest cases check complete unload, thread reuse,
worker cleanup and preservation of the host's unrelated TLS key.

A separate local patch rejects TLS access when master-key allocation fails.
GNU/Linux CTest cases inject base-key allocation failures through RAND and
check host TLS preservation, explicit and process-exit cleanup, and successful
no-fault controls. A separate case initializes the thread destructor key, then
fails master-key allocation during error-state access and checks repeated access,
host TLS preservation and global cleanup.
Partial base initialization releases its thread resources when a subsequent
key allocation fails. RAND and default-context initialization propagate that
failure before allocating more state. Repeated public/private RAND calls and
context creation must fail safely after a base initialization failure.
Plugin-unload checks require the full key capacity to be restored after each
initialization failure, including repeated cleanup.

`OSSL_LIB_CTX_set0_default()` returns NULL when storing the requested thread
default fails and leaves the previous default installed. Passing NULL queries
the current default without changing it.

Local patches prevent allocation-error reporting from re-entering partially
constructed locks, TLS metadata, error-string tables and cleanup registries.
Full configuration-module unload detaches and frees the existing module list
without allocating a replacement list. Partial unload reserves its removal list
before changing module ownership. Other configuration-shutdown phases may still
allocate.

Global `OPENSSL_cleanup()` ends the instance's lifetime and requires every user
to have stopped. `OPENSSL_thread_stop()` releases calling-thread state and permits
subsequent use before global cleanup. Static consumers must relink against the
updated SDK to receive the bundled fixes. These changes preserve the public C API,
shared-library ABI majors and core compatibility floor.

The patches in `cmake/patches/openssl.series` correspond to the following issues:

| Report | Issue | Patch |
| --- | --- | --- |
| 01 | Master TLS key leak on plugin unload | [openssl_thread_cleanup.patch](../cmake/patches/openssl_thread_cleanup.patch) |
| 02 | Failed master-key initialization | [openssl_master_key_init.patch](../cmake/patches/openssl_master_key_init.patch) |
| 03 | Partial base-initialization rollback | [openssl_base_init_rollback.patch](../cmake/patches/openssl_base_init_rollback.patch) |
| 04 | Base-initialization failure propagation | [openssl_base_init_propagation.patch](../cmake/patches/openssl_base_init_propagation.patch) |
| 05 | Default-context TLS store failure | [openssl_default_context_store.patch](../cmake/patches/openssl_default_context_store.patch) |
| 06 | Bootstrap allocation-error recursion | [openssl_bootstrap_allocations.patch](../cmake/patches/openssl_bootstrap_allocations.patch) |
| 07 | Thread-cleanup registration error re-entry | [openssl_thread_registration.patch](../cmake/patches/openssl_thread_registration.patch) |
| 08 | Error-string initialization recursion | [openssl_error_strings.patch](../cmake/patches/openssl_error_strings.patch) |
| 09 | Configuration-module unload allocation failure | [openssl_conf_module_unload.patch](../cmake/patches/openssl_conf_module_unload.patch) |

Apply the series in order. Base-initialization rollback precedes failure
propagation. The master-key check and bootstrap allocation changes precede
the error-state shelving patches, which use the same TLS machinery.

`<cpkt/openssl.h>` is the exhaustive C89-compatible import surface for every
staged top-level OpenSSL public header. It contains OpenSSL's unavoidable
legacy declaration diagnostic locally; it does not relax diagnostics in
consumer source. The typed adapters for native 64-bit values are a separate,
required layer of the facade.

Generic BIO callbacks installed through `cpkt_openssl_BIO_set_callback()` or
`cpkt_openssl_BIO_set_callback_ex()` receive a borrowed, read-only
`cpkt_openssl_bio_mmsg_callback_args` descriptor for SENDMMSG/RECVMMSG and their
RETURN notifications. Its message records are mutable: changes to data pointers,
lengths, addresses, and 64-bit flags propagate to the batch method before it
runs, and back to the caller after a RETURN callback. Changes also propagate
when a callback rejects the operation or the method fails, matching native BIO
behavior. The descriptor and message-record view expire when the callback
returns. Pointer fields remain borrowed; replacement buffers and addresses
must survive the operation and any subsequent caller use. The facade converts
only message metadata; it does not copy or stage payload buffers.

`cpkt_openssl_SSL_poll()` preserves native empty-set behavior: with zero
items, the pointer and stride are unused, a finite timeout is honored, a NULL
timeout returns immediately, and the optional result count becomes zero.
Empty `cpkt_openssl_BIO_sendmmsg()` and `cpkt_openssl_BIO_recvmmsg()` calls
still dispatch through the native BIO, so its method, callbacks, errors and
processed count remain observable. Their processed-count pointer is required
by upstream. Nonempty record spans are checked for pointer, minimum stride,
allocation overflow and stride-span overflow before conversion. Facade
validation failure leaves the caller's count unchanged.

Static and shared strict-C89 record-boundary tests compare these operations
with bundled OpenSSL directly. They cover empty pointer/stride combinations,
NULL and finite timeouts, optional poll counts, high-word flags, padded records,
partial error output, unsupported descriptors, native error queues, empty
batch callback delivery/rejection, legacy and extended pre/post callback
dispatch through native and facade entry points, callback count edits, and
malformed nonempty spans through both public and callback boundaries. They run in
the ordinary CTest, Valgrind and cross-target release suites and native Darwin
CI. This covers facade behavior; it does not retest upstream's QUIC protocol.

The authoritative function inventory is target-specific and is derived from
OpenSSL's pinned `util/libcrypto.num` and `util/libssl.num` files. An entry
marked `EXIST::FUNCTION` belongs to the nominal public ABI. The target's
defined dynamic functions identify which entries are present after optional
algorithms and transports have been configured. The facade surface is the
subset of those functions declared by the configured installed public headers.

The public C89 facade must cover every such declared function, including any
deprecated API the configured headers still expose, and every corresponding
public type, constant, macro, callback, object lifetime operation, and legacy
state operation required to use it. A declaration that already uses C89
language and standard-library types is directly available through
`<cpkt/openssl.h>`; it does not need a redundant `cpkt_openssl_*` wrapper. A
declaration that uses native 64-bit storage, `long long`, or a record/callback
that embeds either must have an explicit typed adapter. A target may not
silently lose such an API. Typed values use the facade's exact eight-byte
representation and adapter functions. Records that embed such values,
including OpenSSL's SSL poll items, have C89 records and adapters too; callers
do not construct native `uint64_t` or `uintptr_t` records.

The batch BIO method callbacks use facade-owned BIO method and BIO shells.
`cpkt_openssl_BIO_new` and `cpkt_openssl_BIO_new_ex` pin their callback context
in OpenSSL's dedicated callback-argument slot; applications must not overwrite
that slot on a facade BIO. Close each facade BIO before its method. A native
BIO reference acquired with `BIO_up_ref` may outlive the facade shell; the
method and batch callback context remain pinned until the last native reference
is freed. Keep application-owned callback context valid until then. A close
attempt from a batch callback, or a method close while a native BIO or callback
remains active, returns zero and leaves ownership unchanged.

Native create and destroy callbacks configured through the borrowed method
may access its facade callback getters/setters or construct another BIO with
that method. Construction reserves the method lifetime before calling OpenSSL
and invokes application callbacks without holding the method lock. Method close
returns zero during construction and native destruction. A failed constructor
releases only its own reservation after native cleanup; other BIOs created by
its callbacks keep their independent ownership. Static and shared regression
tests cover nested creation, create rejection, ex-data allocation failure,
reentrant destruction, and retained native references under Memcheck.

The facade's legacy and extended BIO callback setters use the same pinned
context. When their operation identifies send or receive message batches, the
argument is a borrowed `cpkt_openssl_bio_mmsg_callback_args`; its messages and
the record are valid only for that callback invocation. For other operations,
the argument retains the upstream borrowed byte-pointer semantics.

For the x86_64 GNU OpenSSL 3.6.5 configuration, 6,468 public header
functions are present. This is the raw OpenSSL API inventory, not the
`libcpkt_openssl` export count. Only the mechanically classified typed-adapter
subset (currently 74 direct signatures, plus record-dependent callers) needs
new `cpkt_openssl_*` entry points; the facade's source-controlled dynamic
allowlist currently contains 120 entries. The 6,500-ish OpenSSL dynamic-export
count is not a count of c.pkt facade exports and is not an instruction to
duplicate all of OpenSSL's already-C89 declarations.

The OpenSSL export manifests are not themselves an export allowlist for a
`cpkt_openssl` shared object. The facade needs its own source-controlled,
target-specific defined-export allowlist and separate import check. The only
currently accepted dynamic functions outside OpenSSL's public manifest are
the legacy compatibility or fork hooks `ERR_load_CRYPTO_strings`,
`OCSP_crlID_new`, `OPENSSL_fork_prepare`, `OPENSSL_fork_parent`, and
`OPENSSL_fork_child`; none is declared by the installed public headers and
none belongs to the facade surface. The source-controlled generator also
enumerates the OpenSSL ABI-only DSO, directory, ASN.1, SSL configuration, and
obsolete PEM CMS compatibility exports that the installed public headers omit.

`openssl_api_catalog` is a required target-correct gate. It reports the exact
exported ABI inventory for the selected build, rejects unclassified dynamic
exports, and records the exact typed-adapter subset. The generated facade
coverage check must fail for every typed or record-dependent public catalog
entry lacking an adapter before `cpkt_openssl` ships.
