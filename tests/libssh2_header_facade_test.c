#include <cpkt/libssh2.h>

int main(void) {
  cpkt_libssh2_sftp_handle *handle;
  cpkt_libssh2_u64 value;

  handle = 0;
  (void)sizeof((cpkt_libssh2_sftp_rewind(handle), 0));
  value.high = 0UL;
  value.low = 0UL;
  return value.high != 0UL || value.low != 0UL;
}
