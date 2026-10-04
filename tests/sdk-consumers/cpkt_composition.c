#include <cpkt/openssl.h>
#ifdef CPKT_COMPOSITION_DB
#include <cpkt/sqlite.h>
#endif
#ifdef CPKT_COMPOSITION_MISC
#include <cpkt/sus.h>
#endif

int main(void) {
  SSL_CTX *context;
  cpkt_openssl_u64 value;
  value = cpkt_openssl_u64_make(0UL, 42UL);
  if (cpkt_openssl_u64_low_word(value) != 42UL)
    return 1;
  context = SSL_CTX_new(TLS_client_method());
  if (context == NULL)
    return 2;
  SSL_CTX_free(context);
#ifdef CPKT_COMPOSITION_DB
  {
    cpkt_sqlite *database;
    database = cpkt_sqlite_new(":memory:");
    if (database == NULL)
      return 3;
    if (database->tx(database, "create table composed(value integer)", 0, 0) !=
        CPKT_SQLITE_OK) {
      database->close(database);
      return 4;
    }
    database->close(database);
  }
#endif
#ifdef CPKT_COMPOSITION_MISC
  if (cpkt_sus_backend_version() == NULL ||
      cpkt_sus_backend_capabilities() == NULL)
    return 5;
#endif
  return 0;
}
