# zlib-ng C89 facade

cpkt bundles zlib-ng 2.3.3 in native mode alongside zlib. The libraries are
`libz-ng` and `libcpkt_zlibng`; existing zlib consumers keep `libz`.
The upstream zlib license is retained in the SDK's
`share/doc/cpkt/core/third_party/zlib-ng/LICENSE`.

Include `<cpkt/zlibng.h>` for strict C89 declarations. It covers all 82 public
functions available on the supported Linux and Darwin targets, including
streaming deflate/inflate, dictionaries, stream copying/reset, gzip I/O,
checksums and parameter controls. Windows-only `zng_gzopen_w` is outside the
supported target surface. Native `zng_*` functions map to `cpkt_zlibng_*`;
`zlibng_version` maps to `cpkt_zlibng_version`. Constants use `CPKT_ZLIBNG_`.
`gzgetc` is a function with single argument evaluation.
The five unheadered version/size-checking legacy initializer symbols use their
documented versionless counterparts through the facade.

Use either the facade header or native `zlib-ng.h` in a translation unit.
The facade header can be included with `zlib.h`. It does not include
`zlib-ng.h`, `stdint.h`, or expose `long long`. The implementation uses native
record aliases; public records retain matching tags, field order and compatible
C89 scalar/callback types. Compile-time checks enforce the scalar types, and
the layout test compares each field with independently named upstream records.

Zero-initialize `cpkt_zlibng_stream` to select the default allocator, or set
`zalloc`, `zfree` and `opaque` before initialization. Stream buffers, gzip header
records and their buffers remain borrowed with upstream lifetimes. Each
successful initialization must be paired with its End call; gzip opens must be
paired with close. Callbacks preserve the upstream allocation and streaming
contracts. No complete input or output is buffered by the facade.

`cpkt_zlibng_off64` represents signed 64-bit offsets with unsigned 32-bit `high`
and `low` words in two's-complement bit order. `{0, n}` is a small nonnegative
offset; `{~0U, ~0U}` is -1. Offset conversion preserves all 64 bits on 32-bit and
64-bit targets. Parameter selectors use `CPKT_ZLIBNG_DEFLATE_LEVEL`,
`CPKT_ZLIBNG_DEFLATE_STRATEGY`, and `CPKT_ZLIBNG_DEFLATE_REPRODUCIBLE`; their value
buffers point to `int`, with `size = sizeof(int)` and a per-entry status.

Use `find_package(CpktZlibNg CONFIG REQUIRED)` with `cpkt::zlibng` (static) or
`cpkt::zlibng_facade_shared`. pkg-config exposes `cpkt-zlibng`; use `--static`
for the complete static closure. Native consumers use `find_package(zlib-ng
CONFIG REQUIRED)` with `zlib-ng::zlibstatic` or `zlib-ng::zlib`, or pkg-config
`zlib-ng`. Native metadata supplies gzip and large-file feature definitions.
Facade metadata supplies the static link closure without exposing native
feature definitions to C89 consumers.
The facade's initial shared-library ABI major is 0; existing facade ABIs do not
change.
32-bit ARM builds explicitly retain the hard-float ABI. The upstream compile-only
floating-point probe can otherwise select softfp with a musl toolchain.

## Downstream patch

`cmake/patches/zlib_ng_chorba_heap.patch` is a local change to upstream 2.3.3.
It replaces the large generic Chorba CRC scratch array (128 KiB on 64-bit
targets, 64 KiB on 32-bit targets), the medium generic array (32 KiB), and the
SSE4.1 array (32 KiB) with existing 16-byte-aligned heap allocation helpers.
Buffers are freed after use. If allocation fails, the call computes the same
CRC through the existing braided
implementation, which needs no large scratch buffer. The algorithms remain
enabled; native CPU instruction selection stays portable through runtime
detection, without compiling for the build host's full instruction set.
The upstream modified-build flag is set to 1, including its version-number bit;
the pinned upstream version remains 2.3.3.

Checksum functions have no stream allocator argument and use zlib-ng's internal
allocator. Deflate/inflate retain the stream's supplied allocator callbacks.
The focused regression exercises all three scratch allocations, cleanup and forced
allocation failure. Its pthread uses default attributes, including musl's
default worker-thread stack; the generic path is exercised independently of
the host CPU's preferred CRC implementation.
