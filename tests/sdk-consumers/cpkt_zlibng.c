#define WITH_GZFILEOP 1
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE64_SOURCE 1
#include <zlib-ng.h>

int main(void) {
  static const unsigned char input[] = "123456789";
  return zng_crc32(0, input, 9) != 0xcbf43926U;
}
