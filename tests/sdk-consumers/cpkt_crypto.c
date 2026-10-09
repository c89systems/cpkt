#include <openssl/crypto.h>

int main(void) { return OpenSSL_version(OPENSSL_VERSION) == 0; }
