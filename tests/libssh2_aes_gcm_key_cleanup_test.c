/* Exercise the ownership contract used by Curve25519's key derivation. */
#include "libssh2_priv.h"
#include "mac.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check_cipher(const char *name) {
  LIBSSH2_CRYPT_METHOD cipher;
  const LIBSSH2_MAC_METHOD *mac;
  int direction;

  memset(&cipher, 0, sizeof(cipher));
  cipher.name = name;
  mac = _libssh2_mac_override(&cipher);
  if (mac == NULL || mac->init == NULL || mac->dtor == NULL) {
    fprintf(stderr, "%s: integrated MAC is missing\n", name);
    return 1;
  }
  for (direction = 0; direction < 2; ++direction) {
    unsigned char *key;
    size_t size;
    int free_key;
    int result;
    void *abstract;

    /* Curve25519 allocates key_len plus one SHA-256 digest for each direction.
     */
    size = (size_t)mac->key_len + 32;
    key = malloc(size);
    if (key == NULL)
      return 1;
    memset(key, 0, size);
    free_key = 0;
    abstract = NULL;
    result = mac->init(NULL, key, &free_key, &abstract);
    if (result != 0 || free_key != 1 || abstract != NULL) {
      fprintf(stderr,
              "%s direction %d: unused %lu-byte MAC key was not "
              "returned to the caller\n",
              name, direction, (unsigned long)size);
      free(key);
      return 1;
    }
    /* Honor the callback's ownership result, just as key exchange does. */
    free(key);
    if (mac->dtor(NULL, &abstract) != 0 || abstract != NULL)
      return 1;
  }
  return 0;
}

int main(void) {
  return check_cipher("aes128-gcm@openssh.com") ||
         check_cipher("aes256-gcm@openssh.com");
}
