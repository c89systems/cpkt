# Cyrus SASL C89 facade

`cpkt-sasl` is the C89 receiver-shell boundary for the bundled Cyrus SASL
2.1.28 client and server library. It is distributed separately from raw
`libsasl2` as static and shared libraries, `CpktSasl`, and `cpkt-sasl.pc`.
The static `libsasl2.a` includes the GSSAPI/GS2 mechanism implementation;
static consumers need no loadable SASL modules. The shared SDK ships the
GSSAPI/GS2 modules in `lib/sasl2`, and shared `libsasl2` finds that directory
relative to its own installed location. Downstream distributions must retain
that directory alongside the bundled shared libraries.

`cpkt_sasl_client_new()` and `cpkt_sasl_server_new()` return a receiver whose
methods operate only on that connection. Call `receiver->close(receiver)` once
all provider-owned output and interaction views are no longer needed. Returned
text and interaction views are borrowed from Cyrus SASL and remain valid only
until its documented next operation on that connection or receiver close.

The facade-owned `cpkt_sasl_callbacks` record replaces the upstream
heterogeneous callback table. It is copied at initialization or connection
creation, and the facade retains the copied callbacks for the matching global
SASL lifetime or receiver lifetime. Callback contexts remain caller-owned.
Callbacks must not close their receiver while Cyrus SASL is invoking them.
Global callbacks are valid fallback handlers for connection callbacks; the
facade resolves the current public receiver before invoking them. A callback
selected through a plugin utility retains its selected application context.
Repeated client or server initialization increments the corresponding global
reference count and keeps the callbacks from the first successful
initialization. Each matching finish call releases one reference. A fresh
global lifetime may install new callbacks after the final finish call.
`cpkt_sasl_finish_all()` releases one reference for each role in one call, matching
native `sasl_done`; it retains plugins and callbacks for roles still active.
Close the role's receivers before releasing its final initialization reference.

The `secret` callback returns a borrowed `cpkt_sasl_secret` containing password
bytes and their exact length. The facade copies those bytes into native storage
for the receiver and clears that storage on the next secret callback or receiver
close. Return a null secret with `CPKT_SASL_OK` to cancel. Callback contexts and
the borrowed public secret remain caller-owned.

`encode` and `decode` preserve Cyrus SASL's provider-owned output lifetime;
they do not materialize or concatenate an input stream. Input byte counts use
`unsigned long` and are rejected when they cannot be represented by the
upstream library's bounded unsigned length.

`encode_vector` passes a vector of borrowed byte ranges to Cyrus SASL in one
call; it does not join the ranges. Its output has the same provider-owned
lifetime as `encode`. Property contexts own their request and value storage.
Their returned `cpkt_sasl_property_value` views are borrowed until mutation
or disposal. Repeating `property_get` or `property_getnames` without mutation
does not invalidate an earlier get view. Each `property_get` refreshes its
metadata from the native context, so changes made through another wrapper or
by a plugin appear on the next read. The record array is reused while its
capacity suffices; native name and value pointers retain their native borrow
lifetime. `property_getnames` fills a caller-owned array with one
view per requested name, including names with no values; its return count
includes those names. `auxiliary_context` returns a borrowed wrapper over the
connection's native property context; do not dispose it. The property
functions accept that wrapper while the receiver remains alive.

The receiver property API covers all 23 readable and 10 writable cases in the
pinned `sasl_getprop`/`sasl_setprop` switches. Text and unsigned properties use
the restricted scalar methods; security and HTTP records are copied into
caller-owned C89 records with borrowed nested pointers. The callback-table
getter returns the copied local application record, and the option-context
getter unwraps an application callback owner when Cyrus selected one. The
generic delegated-payload getter returns the mechanism's opaque borrowed
payload unchanged. The typed GSS take operation first verifies that the
selected mechanism and plugin source are the pinned GSSAPI or GS2 provider;
an incompatible custom mechanism returns `CPKT_SASL_BADPROT` without reading
its payload. External auth and endpoint setters copy text in Cyrus. A null
external auth, endpoint or application name clears it; default realm requires
a nonempty server-side value. HTTP and channel-binding setters retain borrowed
nested data through the connection's use of those properties.

`cpkt/sasl_plugin.h` defines C89 records for custom client, server,
canonicalizer, and auxiliary-property plugins. Registration copies plugin
records and keeps their callback contexts caller-owned until the matching
client or server finish. Connection callbacks receive converted parameter and
output records. Their views last for the callback; output byte ranges remain
caller-owned for the period required by the native mechanism contract.
Security-layer encode/decode callbacks receive vector or byte views without
whole-message staging. A single formatted log or error record can be
materialized by Cyrus SASL; the facade forwards one record at a time.

`cpkt_sasl_idle(NULL)` requests native global precomputation; the receiver
method handles a particular connection. The pinned Cyrus idle callbacks pass
the native connection itself, so the C89 plugin callback receives its public
`cpkt_sasl` receiver, rather than the state returned by `new_connection`.
Global client precomputation supplies null receiver and parameter pointers.
The native server global hook reports work without calling individual plugins.

Empty client/server plugin lists are valid. Server initializer `CONTINUE` and
`NOUSER` results retain their native mechanism condition. Global cleanup runs
once per plugin record, as in Cyrus; records sharing application context must
coordinate their own reference counts. No cleanup calls are collapsed merely
because context pointers are equal.
If native registration fails after adopting some records, those records stay
registered and receive their normal native `free_context` callback at role
shutdown. Each unadopted record is released immediately, while its persistent
global utility table is still valid. Canonicalizer and auxiliary registrations
are atomic; an unadopted record is likewise released before add returns.

The plugin utility record binds allocator, mutex, MD5/HMAC, random, property,
callback, logging, error, base64, and SASL connection utilities. Its receiver
pointer is borrowed and is null during global plugin initialization. Global
utility tables remain valid through `mech_free`; per-connection tables remain
valid through connection disposal. `get_callbacks` reports the actual native
selection, adapting native default handlers and preserving facade callback
contexts. `option_context` contains the configured application's context when
available, and is null for provider-only option dispatch. The receiver's
`get_option_context` chooses its local option callback context, then its global
callback context, and returns null if neither exists. The option callback
adapter still invokes the provider dispatcher so configuration fallback works.
The utility's scalar property methods and typed `set_text_property` dispatch
through Cyrus's utility slots. Its borrowed `connection` receiver provides
the nested security/HTTP, callback-record, option-context, GSS and channel
binding operations. As with the native utilities, connection property calls
are invalid during global plugin initialization.
MD5/HMAC
contexts and property wrappers are converted field by field; they do not alias
unrelated native record layouts. The MD5 implementation is the bundled Cyrus
SASL implementation of the **RSA Data Security, Inc. MD5 Message-Digest
Algorithm**. No digest algorithm implementation is copied into this facade.

The typed GSS credential setters and getters refer to opaque GSS handles.
The input credential stays caller-owned. A delegated credential is returned by
Cyrus as a pointer to a GSS handle cell. The typed take operation reads and
clears that cell, transferring the handle once; release it through the GSS
facade. Custom plugins can carry their own mechanism-specific opaque payload
in `client_credentials`; it is not interpreted as a GSS handle by registration.
Peer and local GSS names are borrowed until the next SASL step or receiver
close. Channel-binding and HTTP request byte pointers supplied to setters
must remain valid while the connection may use those properties.

The checked-in auth contract snapshots the configured native declarations,
callback records and slots, constants, and typedefs for every SDK target.
Typed C89 binding and numeric parity probes run with each configured target
compiler. A separate public-signature snapshot freezes every facade function,
record field and callback type per target. Export allowlists and extracted package checks constrain the shared
boundary to the public facade symbols.
`auth_sasl_property_contract` compares each pinned implementation switch case
with a maintained typed receiver and utility path, so a scalar `sasl_getprop`
mapping cannot conceal a missing record or callback property.

Plugin-info callbacks expose the complete C89 plugin record and application
context for mechanisms registered through this facade. Bundled native plugins
remain inspectable through metadata; their native callback ABI is not a C89
application callback and no callable facade record is returned for them.

When `start` or `step` returns `CPKT_SASL_INTERACT`, the caller fills the
borrowed interaction records and calls that same operation again with the
same interaction pointer. The facade forwards the completed records to Cyrus
SASL; they remain invalid after the next operation or receiver close.
