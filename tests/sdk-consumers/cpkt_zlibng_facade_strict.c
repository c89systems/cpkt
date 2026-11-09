#include <cpkt/zlibng.h>
#include <string.h>

#if defined(WITH_GZFILEOP) || defined(ZLIBNG_NATIVE_API)
#error Native zlib-ng feature definitions must stay behind the C89 facade
#endif

int main(void) {
  static const unsigned char input[] = "C89 installed zlib-ng consumer";
  unsigned char packed[128];
  unsigned char output[128];
  size_t packed_size;
  size_t output_size;
  packed_size = sizeof(packed);
  output_size = sizeof(output);
  if (cpkt_zlibng_compress(packed, &packed_size, input, sizeof(input)) != 0)
    return 1;
  if (cpkt_zlibng_uncompress(output, &output_size, packed, packed_size) != 0)
    return 2;
  return output_size != sizeof(input) ||
         memcmp(output, input, sizeof(input)) != 0;
}
