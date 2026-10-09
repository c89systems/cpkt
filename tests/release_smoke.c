#include <cpkt/openssl.h>

int main(void) {
  cpkt_openssl_u64 value;
  SSL_CTX *context;

  value = cpkt_openssl_u64_make(0UL, 1UL);
  context = SSL_CTX_new(TLS_client_method());
  if (context == 0)
    return 1;
  SSL_CTX_free(context);
  return cpkt_openssl_u64_low_word(value) == 1UL ? 0 : 1;
}
