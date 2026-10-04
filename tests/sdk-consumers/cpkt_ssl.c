#include <openssl/ssl.h>

int main(void) {
  SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
  if (ctx == 0) {
    return 1;
  }
  SSL_CTX_free(ctx);
  return 0;
}
