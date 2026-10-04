#include <cpkt/gssapi.h>

int main(void) {
  cpkt_gss_oid_set *mechanisms;
  cpkt_gss_buffer_set *buffers;
  cpkt_gss_krb_context *kerberos;
  cpkt_gss_status minor;
  cpkt_gss_status status;

  mechanisms = 0;
  buffers = 0;
  kerberos = 0;
  if (cpkt_gss_known_oid(CPKT_GSS_OID_HOSTBASED_SERVICE_X) == 0 ||
      cpkt_gss_create_buffer_set(&minor, &buffers) != CPKT_GSS_COMPLETE ||
      buffers == 0 ||
      cpkt_gss_release_buffer_set(&minor, &buffers) != CPKT_GSS_COMPLETE ||
      cpkt_gss_krb_context_new(&kerberos) != 0 || kerberos == 0) {
    return 2;
  }
  cpkt_gss_krb_context_free(&kerberos);
  status = cpkt_gss_indicate_mechanisms(&minor, &mechanisms);
  if (cpkt_gss_status_is_error(status) || mechanisms == 0 ||
      cpkt_gss_oid_set_count(mechanisms) == 0 ||
      cpkt_gss_name_type_hostbased_service() == 0) {
    return 1;
  }
  status = cpkt_gss_release_oid_set(&minor, &mechanisms);
  return cpkt_gss_status_is_error(status) || mechanisms != 0;
}
