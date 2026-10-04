#include <cpkt/gssapi.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int missing_handle_outputs(void) {
  cpkt_gss_name *name;
  cpkt_gss_buffer input, oid_text, empty;
  cpkt_gss_status status, minor, ignored;
  cpkt_gss_status *minor_out;
  const cpkt_gss_oid *mechanism;
  int index, omit_minor, failure;
  name = NULL;
  input.value = (void *)"test-user@EXAMPLE.TEST";
  input.length = strlen((const char *)input.value);
  oid_text.value = (void *)"{ 1 2 3 }";
  oid_text.length = 9;
  empty.value = NULL;
  empty.length = 0;
  status =
      cpkt_gss_import_name(&minor, &input, cpkt_gss_name_type_user(), &name);
  if (cpkt_gss_status_is_error(status) || name == NULL)
    return 1;
  mechanism = cpkt_gss_oid_set_at(cpkt_gss_krb5_mechanism_set(0), 0);
  failure = 0;
  for (omit_minor = 0; omit_minor < 2; ++omit_minor) {
    minor_out = omit_minor ? NULL : &minor;
    for (index = 0; index < 9; ++index) {
      minor = 99;
      switch (index) {
      case 0:
        status = cpkt_gss_indicate_mechanisms(minor_out, NULL);
        break;
      case 1:
        status = cpkt_gss_create_oid_set(minor_out, NULL);
        break;
      case 2:
        status = cpkt_gss_oid_from_text(minor_out, &oid_text, NULL);
        break;
      case 3:
        status = cpkt_gss_import_name(minor_out, &input,
                                      cpkt_gss_name_type_user(), NULL);
        break;
      case 4:
        status = cpkt_gss_duplicate_name(minor_out, name, NULL);
        break;
      case 5:
        status = cpkt_gss_canonicalize_name(minor_out, name, mechanism, NULL);
        break;
      case 6:
        status = cpkt_gss_add_oid_to_set(minor_out, mechanism, NULL);
        break;
      case 7:
        status =
            cpkt_gss_init_context(minor_out, NULL, NULL, name, mechanism, 0, 0,
                                  NULL, NULL, NULL, NULL, NULL, NULL);
        break;
      default:
        status = cpkt_gss_accept_context(minor_out, NULL, NULL, &empty, NULL,
                                         NULL, NULL, NULL, NULL, NULL, NULL);
        break;
      }
      if (status != CPKT_GSS_S_CALL_BAD_STRUCTURE ||
          (!omit_minor && minor != EINVAL)) {
        fprintf(stderr,
                "missing GSS handle output: case=%d status=%lu minor=%lu\n",
                index, status, minor);
        failure = index + 1;
        break;
      }
    }
    if (failure)
      break;
  }
  cpkt_gss_release_name(&ignored, &name);
  return failure;
}

int main(void) {
  cpkt_gss_oid_set *mechanisms;
  cpkt_gss_oid *oid;
  cpkt_gss_buffer oid_text;
  cpkt_gss_buffer encoded;
  cpkt_gss_status minor;
  cpkt_gss_status status;
  cpkt_gss_buffer empty;
  cpkt_gss_qop qop;
  int confidentiality;
  cpkt_gss_lifetime lifetime;
  int present;

  if (missing_handle_outputs() != 0)
    return 9;

  empty.length = 0;
  empty.value = 0;
  qop = 99;
  status = cpkt_gss_verify_mic(&minor, 0, &empty, &empty, &qop);
  if (!cpkt_gss_status_is_error(status) || qop != 0)
    return 5;
  confidentiality = 99;
  status = cpkt_gss_wrap(&minor, 0, 1, 0, &empty, &confidentiality, 0);
  if (!cpkt_gss_status_is_error(status) || confidentiality != 0)
    return 6;
  confidentiality = 99;
  qop = 99;
  status = cpkt_gss_unwrap(&minor, 0, &empty, 0, &confidentiality, &qop);
  if (!cpkt_gss_status_is_error(status) || confidentiality != 0 || qop != 0)
    return 7;
  lifetime = 99;
  status = cpkt_gss_context_lifetime(&minor, 0, &lifetime);
  if (!cpkt_gss_status_is_error(status) || lifetime != 0)
    return 8;

  mechanisms = 0;
  status = cpkt_gss_indicate_mechanisms(&minor, &mechanisms);
  if (cpkt_gss_status_is_error(status) || mechanisms == 0 ||
      cpkt_gss_oid_set_count(mechanisms) == 0) {
    return 1;
  }
  oid_text.length = 9;
  oid_text.value = (void *)"{ 1 2 3 }";
  oid = 0;
  status = cpkt_gss_oid_from_text(&minor, &oid_text, &oid);
  if (cpkt_gss_status_is_error(status) || oid == 0) {
    cpkt_gss_release_oid_set(&minor, &mechanisms);
    return 2;
  }
  encoded.length = 0;
  encoded.value = 0;
  status = cpkt_gss_oid_to_text(&minor, oid, &encoded);
  if (cpkt_gss_status_is_error(status) || encoded.value == 0 ||
      encoded.length == 0) {
    cpkt_gss_release_oid(&minor, &oid);
    cpkt_gss_release_oid_set(&minor, &mechanisms);
    return 3;
  }
  cpkt_gss_release_buffer(&minor, &encoded);
  present = 1;
  status = cpkt_gss_oid_set_contains(&minor, oid, mechanisms, &present);
  if (cpkt_gss_status_is_error(status)) {
    cpkt_gss_release_oid(&minor, &oid);
    cpkt_gss_release_oid_set(&minor, &mechanisms);
    return 4;
  }
  cpkt_gss_release_oid(&minor, &oid);
  cpkt_gss_release_oid_set(&minor, &mechanisms);
  return 0;
}
