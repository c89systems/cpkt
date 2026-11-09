#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#define WITH_GZFILEOP 1
/* Compare the public C89 records with independently named native definitions.
 */
#define zng_stream_s cpkt_test_native_stream
#define zng_gz_header_s cpkt_test_native_header
#include <zlib-ng.h>
#undef zng_stream_s
#undef zng_gz_header_s
#include <cpkt/zlibng.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define RECORD(public_type, native_type)                                       \
  typedef char                                                                 \
      record_##public_type[(sizeof(public_type) == sizeof(native_type)) ? 1    \
                                                                        : -1]
#define FIELD(public_type, native_type, field)                                 \
  typedef char field_##public_type##_##field                                   \
      [offsetof(public_type, field) == offsetof(native_type, field) &&         \
               sizeof(((public_type *)0)->field) ==                            \
                   sizeof(((native_type *)0)->field)                           \
           ? 1                                                                 \
           : -1]

RECORD(cpkt_zlibng_stream, zng_stream);
FIELD(cpkt_zlibng_stream, zng_stream, next_in);
FIELD(cpkt_zlibng_stream, zng_stream, avail_in);
FIELD(cpkt_zlibng_stream, zng_stream, total_in);
FIELD(cpkt_zlibng_stream, zng_stream, next_out);
FIELD(cpkt_zlibng_stream, zng_stream, avail_out);
FIELD(cpkt_zlibng_stream, zng_stream, total_out);
FIELD(cpkt_zlibng_stream, zng_stream, msg);
FIELD(cpkt_zlibng_stream, zng_stream, state);
FIELD(cpkt_zlibng_stream, zng_stream, zalloc);
FIELD(cpkt_zlibng_stream, zng_stream, zfree);
FIELD(cpkt_zlibng_stream, zng_stream, opaque);
FIELD(cpkt_zlibng_stream, zng_stream, data_type);
FIELD(cpkt_zlibng_stream, zng_stream, adler);
FIELD(cpkt_zlibng_stream, zng_stream, reserved);
RECORD(cpkt_zlibng_gz_header, zng_gz_header);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, text);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, time);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, xflags);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, os);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, extra);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, extra_len);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, extra_max);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, name);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, name_max);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, comment);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, comm_max);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, hcrc);
FIELD(cpkt_zlibng_gz_header, zng_gz_header, done);
RECORD(cpkt_zlibng_deflate_param_value, zng_deflate_param_value);
FIELD(cpkt_zlibng_deflate_param_value, zng_deflate_param_value, param);
FIELD(cpkt_zlibng_deflate_param_value, zng_deflate_param_value, buf);
FIELD(cpkt_zlibng_deflate_param_value, zng_deflate_param_value, size);
FIELD(cpkt_zlibng_deflate_param_value, zng_deflate_param_value, status);

int main(void) {
  cpkt_zlibng_off64 large = {1, 7};
  cpkt_zlibng_off64 negative = {~0U, ~0U};
  z_off64_t native_large = ((z_off64_t)1 << 32) + 7;
  FILE *temporary;
  char path[] = "zlibng-offset-XXXXXX";
  int descriptor;
  cpkt_zlibng_gzfile gzip;
  cpkt_zlibng_off64 position;
  if (cpkt_zlibng_crc32_combine(123, 456, large) !=
          zng_crc32_combine(123, 456, native_large) ||
      cpkt_zlibng_adler32_combine(123, 456, negative) !=
          zng_adler32_combine(123, 456, (z_off64_t)-1))
    return 1;
  descriptor = mkstemp(path);
  if (descriptor < 0)
    return 2;
  unlink(path);
  temporary = fdopen(descriptor, "w+b");
  if (temporary == NULL || ftruncate(fileno(temporary), native_large) != 0)
    return 2;
  gzip = cpkt_zlibng_gzdopen(dup(fileno(temporary)), "rb");
  if (gzip == NULL)
    return 3;
  position = cpkt_zlibng_gzseek(gzip, large, SEEK_SET);
  if (position.high != large.high || position.low != large.low)
    return 4;
  position = cpkt_zlibng_gztell(gzip);
  if (position.high != large.high || position.low != large.low)
    return 5;
  if (cpkt_zlibng_gzclose(gzip) != 0 || fclose(temporary) != 0)
    return 6;
  return 0;
}
