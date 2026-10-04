#include <cpkt/gssapi.h>
#include <gssapi/gssapi_ext.h>

#include <string.h>

int main(void) {
  const cpkt_gss_oid *facade =
      cpkt_gss_known_oid(CPKT_GSS_OID_INQ_ODBC_SESSION_KEY);
  const void *bytes = NULL;
  size_t length = 0;
  if (facade == NULL || GSS_C_INQ_ODBC_SESSION_KEY == NULL ||
      !cpkt_gss_oid_view(facade, &bytes, &length) ||
      length != GSS_C_INQ_ODBC_SESSION_KEY->length ||
      memcmp(bytes, GSS_C_INQ_ODBC_SESSION_KEY->elements, length) != 0)
    return 1;
  return 0;
}
