#include <cpkt/gssapi.h>

#include <errno.h>
#include <limits.h>
#include <string.h>

int main(void) {
  cpkt_gss_status major, minor;
  cpkt_gss_name *name = 0, *copy = 0;
  cpkt_gss_buffer input, output, element;
  cpkt_gss_buffer_set *set = 0;
  cpkt_gss_krb_context *krb = 0;
  cpkt_gss_krb_cache *cache = 0;
  cpkt_gss_krb_keytab *keytab = 0;
  cpkt_gss_krb_principal *principal = 0;
  const cpkt_gss_oid *oid;
  const void *bytes;
  size_t length;
  int equal, index;
  char *allocated;

  allocated = cpkt_gss_allocation_strdup("owned");
  if (allocated == 0 || strcmp(allocated, "owned") != 0)
    return 18;
  allocated = (char *)cpkt_gss_allocation_realloc(allocated, 16);
  if (allocated == 0 || strcmp(allocated, "owned") != 0)
    return 19;
  cpkt_gss_allocation_free(allocated);

  for (index = CPKT_GSS_OID_HOSTBASED_SERVICE_X;
       index <= CPKT_GSS_OID_KRB5_GET_CRED_IMPERSONATOR; ++index) {
    oid = cpkt_gss_known_oid(index);
    if (oid == 0 || !cpkt_gss_oid_view(oid, &bytes, &length) || bytes == 0 ||
        length == 0)
      return 1;
  }
  if (cpkt_gss_known_oid(-1) != 0 || cpkt_gss_krb5_mechanism_set(0) == 0 ||
      cpkt_gss_oid_set_count(cpkt_gss_krb5_mechanism_set(0)) == 0)
    return 2;
  oid = 0;
  {
    const unsigned char der[] = {0x2a, 0x03, 0x04};
    cpkt_gss_oid *owned = 0;
    if (cpkt_gss_oid_from_bytes(&minor, der, sizeof(der), &owned) !=
            CPKT_GSS_COMPLETE ||
        owned == 0 || !cpkt_gss_oid_view(owned, &bytes, &length) ||
        length != sizeof(der) || memcmp(bytes, der, sizeof(der)) != 0 ||
        cpkt_gss_status_is_error(cpkt_gss_release_oid(&minor, &owned)) ||
        owned != 0 ||
        !cpkt_gss_status_is_error(
            cpkt_gss_oid_from_bytes(&minor, 0, 3, &owned)))
      return 21;
  }
  input.value = (void *)"test-user";
  input.length = 9;
  major =
      cpkt_gss_import_name(&minor, &input, cpkt_gss_name_type_user(), &name);
  if (cpkt_gss_status_is_error(major) || name == 0)
    return 3;
  major = cpkt_gss_duplicate_name(&minor, name, &copy);
  if (cpkt_gss_status_is_error(major) || copy == 0)
    return 4;
  equal = 0;
  major = cpkt_gss_compare_names(&minor, name, copy, &equal);
  if (cpkt_gss_status_is_error(major) || !equal)
    return 5;
  output.value = 0;
  output.length = 0;
  major = cpkt_gss_display_name(&minor, name, &output, 0);
  if (cpkt_gss_status_is_error(major) || output.length != input.length ||
      memcmp(output.value, input.value, input.length) != 0)
    return 6;
  if (cpkt_gss_status_is_error(cpkt_gss_release_buffer(&minor, &output)) ||
      cpkt_gss_status_is_error(cpkt_gss_release_name(&minor, &copy)) ||
      cpkt_gss_status_is_error(cpkt_gss_release_name(&minor, &name)))
    return 7;

  major = cpkt_gss_create_buffer_set(&minor, &set);
  if (cpkt_gss_status_is_error(major) || set == 0)
    return 8;
  major = cpkt_gss_add_buffer_set_member(&minor, &input, &set);
  if (cpkt_gss_status_is_error(major) || cpkt_gss_buffer_set_count(set) != 1 ||
      !cpkt_gss_buffer_set_at(set, 0, &element) ||
      element.length != input.length ||
      memcmp(element.value, input.value, input.length) != 0)
    return 9;
  if (cpkt_gss_status_is_error(cpkt_gss_release_buffer_set(&minor, &set)) ||
      set != 0)
    return 10;

  if (cpkt_gss_krb_context_new(&krb) != 0 || krb == 0)
    return 11;
  if (cpkt_gss_krb_cache_resolve(krb, "MEMORY:cpkt-gss-test", &cache) != 0 ||
      cache == 0)
    return 12;
  if (cpkt_gss_krb_keytab_resolve(krb, "FILE:/nonexistent/cpkt-test.keytab",
                                  &keytab) != 0 ||
      keytab == 0)
    return 13;
  if (cpkt_gss_krb_principal_parse(krb, "test-user@EXAMPLE.TEST", &principal) !=
          0 ||
      principal == 0)
    return 14;
  cpkt_gss_krb_principal_free(krb, &principal);
  if (principal != 0 || cpkt_gss_krb_keytab_close(krb, &keytab) != 0 ||
      keytab != 0 || cpkt_gss_krb_cache_close(krb, &cache) != 0 || cache != 0)
    return 15;
  cpkt_gss_krb_context_free(&krb);
  if (krb != 0)
    return 16;

#if ULONG_MAX > 0xffffffffUL
  {
    cpkt_gss_iov iov;
    cpkt_gss_channel_bindings bindings;
    iov.type = 0x100000000UL;
    iov.buffer = input;
    major = cpkt_gss_release_iov(&minor, &iov, 1);
    if (!cpkt_gss_status_is_error(major) || iov.type != 0x100000000UL)
      return 17;
    if (cpkt_gss_wrap_iov(&minor, 0, 0, 0x100000000UL, 0, &iov, 1) !=
            CPKT_GSS_S_CALL_BAD_STRUCTURE ||
        minor != EINVAL)
      return 20;
    if (cpkt_gss_get_mic(&minor, 0, 0x100000000UL, &input, &output) !=
            CPKT_GSS_S_CALL_BAD_STRUCTURE ||
        minor != EINVAL)
      return 21;
    if (cpkt_gss_acquire_credential(&minor, 0, 0x100000000UL, 0, 0, 0, 0, 0) !=
            CPKT_GSS_S_CALL_BAD_STRUCTURE ||
        minor != EINVAL)
      return 22;
    memset(&bindings, 0, sizeof(bindings));
    bindings.initiator_address_type = 0x100000000UL;
    if (cpkt_gss_init_context(&minor, 0, 0, 0, 0, 0, 0, &bindings, 0, 0, 0, 0,
                              0) != CPKT_GSS_S_CALL_BAD_STRUCTURE ||
        minor != EINVAL)
      return 23;
    if (!cpkt_gss_status_is_error(0x100000000UL))
      return 24;
  }
#endif
  return 0;
}
