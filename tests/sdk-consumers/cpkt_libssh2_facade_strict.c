#include <cpkt/libssh2.h>

int main(void) {
  cpkt_libssh2_u64 value;

  value.high = 0UL;
  value.low = 0UL;
  return cpkt_libssh2_version(0) == 0 || value.high != 0UL;
}
