#include <cpkt/libssh2.h>
#include <libssh2_sftp.h>

#include <string.h>

int __wrap_libssh2_sftp_readdir_ex(LIBSSH2_SFTP_HANDLE *handle, char *buffer,
                                   size_t buffer_maxlen, char *longentry,
                                   size_t longentry_maxlen,
                                   LIBSSH2_SFTP_ATTRIBUTES *attrs) {
  (void)handle;
  (void)buffer;
  (void)buffer_maxlen;
  (void)longentry;
  (void)longentry_maxlen;
  (void)attrs;
  return 0;
}

int main(void) {
  cpkt_libssh2_sftp_attributes attributes;
  cpkt_libssh2_sftp_attributes expected;
  char name[8];

  memset(&attributes, 0x5a, sizeof(attributes));
  memcpy(&expected, &attributes, sizeof(expected));
  if (cpkt_libssh2_sftp_readdir_ex(NULL, name, sizeof(name), NULL, 0,
                                   &attributes) != 0 ||
      memcmp(&attributes, &expected, sizeof(attributes)) != 0)
    return 1;
  return 0;
}
