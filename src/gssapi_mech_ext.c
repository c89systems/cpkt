#include <cpkt/gssapi.h>

#include <errno.h>
#include <sys/types.h>

#include <gssapi/gssapi_ext.h>

static cpkt_gss_status cpkt_gss_mech_result(OM_uint32 status, OM_uint32 minor,
                                            cpkt_gss_status *minor_out) {
  if (minor_out != NULL)
    *minor_out = minor;
  return status;
}

static cpkt_gss_status cpkt_gss_mech_bad_argument(cpkt_gss_status *minor_out) {
  return cpkt_gss_mech_result(GSS_S_CALL_BAD_STRUCTURE, EINVAL, minor_out);
}

static gss_buffer_desc cpkt_gss_mech_buffer(const cpkt_gss_buffer *buffer) {
  gss_buffer_desc native;
  native.length = buffer == NULL ? 0 : buffer->length;
  native.value = buffer == NULL ? NULL : buffer->value;
  return native;
}

static void cpkt_gss_mech_publish(cpkt_gss_buffer *output,
                                  gss_buffer_desc *native) {
  OM_uint32 ignored;
  if (output != NULL) {
    output->length = native->length;
    output->value = native->value;
  } else if (native->value != NULL) {
    (void)gss_release_buffer(&ignored, native);
  }
}

/** C89 facade contract for cpkt_gss_indicate_mechanisms_by_attributes; see the
 * public header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_indicate_mechanisms_by_attributes(
    cpkt_gss_status *minor_out, const cpkt_gss_oid_set *desired,
    const cpkt_gss_oid_set *excluded, const cpkt_gss_oid_set *critical,
    cpkt_gss_oid_set **mechanisms_out) {
  gss_OID_set native = GSS_C_NO_OID_SET;
  OM_uint32 minor, status;
  if (mechanisms_out == NULL)
    return cpkt_gss_mech_bad_argument(minor_out);
  *mechanisms_out = NULL;
  status = gss_indicate_mechs_by_attrs(&minor, (gss_OID_set)desired,
                                       (gss_OID_set)excluded,
                                       (gss_OID_set)critical, &native);
  *mechanisms_out = (cpkt_gss_oid_set *)native;
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_inquire_mechanism_attributes; see the
 * public header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_inquire_mechanism_attributes(
    cpkt_gss_status *minor_out, const cpkt_gss_oid *mechanism,
    cpkt_gss_oid_set **supported_out, cpkt_gss_oid_set **known_out) {
  gss_OID_set supported = GSS_C_NO_OID_SET, known = GSS_C_NO_OID_SET;
  OM_uint32 minor, ignored;
  OM_uint32 status = gss_inquire_attrs_for_mech(&minor, (gss_OID)mechanism,
                                                &supported, &known);
  if (supported_out != NULL)
    *supported_out = (cpkt_gss_oid_set *)supported;
  else if (supported != GSS_C_NO_OID_SET)
    (void)gss_release_oid_set(&ignored, &supported);
  if (known_out != NULL)
    *known_out = (cpkt_gss_oid_set *)known;
  else if (known != GSS_C_NO_OID_SET)
    (void)gss_release_oid_set(&ignored, &known);
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_display_mechanism_attribute; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_display_mechanism_attribute(
    cpkt_gss_status *minor_out, const cpkt_gss_oid *attribute,
    cpkt_gss_buffer *name_out, cpkt_gss_buffer *short_description_out,
    cpkt_gss_buffer *long_description_out) {
  gss_buffer_desc name = {0, NULL}, short_desc = {0, NULL},
                  long_desc = {0, NULL};
  OM_uint32 minor;
  OM_uint32 status = gss_display_mech_attr(&minor, (gss_OID)attribute, &name,
                                           &short_desc, &long_desc);
  cpkt_gss_mech_publish(name_out, &name);
  cpkt_gss_mech_publish(short_description_out, &short_desc);
  cpkt_gss_mech_publish(long_description_out, &long_desc);
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_mechanism_for_sasl_name; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status
cpkt_gss_mechanism_for_sasl_name(cpkt_gss_status *minor_out,
                                 const cpkt_gss_buffer *sasl_name,
                                 const cpkt_gss_oid **mechanism_out) {
  gss_buffer_desc name = cpkt_gss_mech_buffer(sasl_name);
  gss_OID mechanism = GSS_C_NO_OID;
  OM_uint32 minor, status;
  if (mechanism_out == NULL)
    return cpkt_gss_mech_bad_argument(minor_out);
  *mechanism_out = NULL;
  status = gss_inquire_mech_for_saslname(&minor, &name, &mechanism);
  *mechanism_out = (const cpkt_gss_oid *)mechanism;
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_sasl_name_for_mechanism; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_sasl_name_for_mechanism(
    cpkt_gss_status *minor_out, const cpkt_gss_oid *mechanism,
    cpkt_gss_buffer *sasl_name_out, cpkt_gss_buffer *mechanism_name_out,
    cpkt_gss_buffer *description_out) {
  gss_buffer_desc sasl_name = {0, NULL}, name = {0, NULL},
                  description = {0, NULL};
  OM_uint32 minor;
  OM_uint32 status = gss_inquire_saslname_for_mech(
      &minor, (gss_OID)mechanism, &sasl_name, &name, &description);
  cpkt_gss_mech_publish(sasl_name_out, &sasl_name);
  cpkt_gss_mech_publish(mechanism_name_out, &name);
  cpkt_gss_mech_publish(description_out, &description);
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_oid_equal; see the public header for
 * ownership and callback lifetime. */
int cpkt_gss_oid_equal(const cpkt_gss_oid *left, const cpkt_gss_oid *right) {
  return gss_oid_equal((gss_OID)left, (gss_OID)right);
}

/** C89 facade contract for cpkt_gss_inquire_context_by_oid; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_inquire_context_by_oid(
    cpkt_gss_status *minor_out, const cpkt_gss_context *context,
    const cpkt_gss_oid *object, cpkt_gss_buffer_set **data_out) {
  gss_buffer_set_t native = GSS_C_NO_BUFFER_SET;
  OM_uint32 minor, status;
  if (data_out == NULL)
    return cpkt_gss_mech_bad_argument(minor_out);
  *data_out = NULL;
  status = gss_inquire_sec_context_by_oid(&minor, (gss_ctx_id_t)context,
                                          (gss_OID)object, &native);
  *data_out = (cpkt_gss_buffer_set *)native;
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_set_context_option; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_set_context_option(cpkt_gss_status *minor_out,
                                            cpkt_gss_context **context,
                                            const cpkt_gss_oid *option,
                                            const cpkt_gss_buffer *value) {
  gss_ctx_id_t native;
  gss_buffer_desc native_value = cpkt_gss_mech_buffer(value);
  OM_uint32 minor, status;
  if (context == NULL)
    return cpkt_gss_mech_bad_argument(minor_out);
  native = (gss_ctx_id_t)*context;
  status = gss_set_sec_context_option(&minor, &native, (gss_OID)option,
                                      &native_value);
  *context = (cpkt_gss_context *)native;
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_wrap_aead; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status
cpkt_gss_wrap_aead(cpkt_gss_status *minor_out, const cpkt_gss_context *context,
                   int confidentiality_requested, cpkt_gss_qop qop,
                   const cpkt_gss_buffer *associated_data,
                   const cpkt_gss_buffer *plaintext, int *confidentiality_out,
                   cpkt_gss_buffer *token_out) {
  gss_buffer_desc assoc = cpkt_gss_mech_buffer(associated_data);
  gss_buffer_desc input = cpkt_gss_mech_buffer(plaintext);
  gss_buffer_desc output = {0, NULL};
  int conf = 0;
  OM_uint32 minor;
  OM_uint32 status;
  if (qop > 0xffffffffUL)
    return cpkt_gss_mech_bad_argument(minor_out);
  status =
      gss_wrap_aead(&minor, (gss_ctx_id_t)context, confidentiality_requested,
                    (gss_qop_t)qop, &assoc, &input, &conf, &output);
  if (confidentiality_out != NULL)
    *confidentiality_out = conf;
  cpkt_gss_mech_publish(token_out, &output);
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_unwrap_aead; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_unwrap_aead(cpkt_gss_status *minor_out,
                                     const cpkt_gss_context *context,
                                     const cpkt_gss_buffer *token,
                                     const cpkt_gss_buffer *associated_data,
                                     cpkt_gss_buffer *plaintext_out,
                                     int *confidentiality_out,
                                     cpkt_gss_qop *qop_out) {
  gss_buffer_desc input = cpkt_gss_mech_buffer(token);
  gss_buffer_desc assoc = cpkt_gss_mech_buffer(associated_data);
  gss_buffer_desc output = {0, NULL};
  gss_qop_t qop = 0;
  int conf = 0;
  OM_uint32 minor;
  OM_uint32 status = gss_unwrap_aead(&minor, (gss_ctx_id_t)context, &input,
                                     &assoc, &output, &conf, &qop);
  if (confidentiality_out != NULL)
    *confidentiality_out = conf;
  if (qop_out != NULL)
    *qop_out = qop;
  cpkt_gss_mech_publish(plaintext_out, &output);
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_encapsulate_token; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_encapsulate_token(const cpkt_gss_buffer *input,
                                           const cpkt_gss_oid *oid,
                                           cpkt_gss_buffer *output) {
  gss_buffer_desc native_input = cpkt_gss_mech_buffer(input);
  gss_buffer_desc native_output = {0, NULL};
  OM_uint32 status =
      gss_encapsulate_token(&native_input, (gss_OID)oid, &native_output);
  cpkt_gss_mech_publish(output, &native_output);
  return status;
}

/** C89 facade contract for cpkt_gss_decapsulate_token; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_decapsulate_token(const cpkt_gss_buffer *input,
                                           const cpkt_gss_oid *oid,
                                           cpkt_gss_buffer *output) {
  gss_buffer_desc native_input = cpkt_gss_mech_buffer(input);
  gss_buffer_desc native_output = {0, NULL};
  OM_uint32 status =
      gss_decapsulate_token(&native_input, (gss_OID)oid, &native_output);
  cpkt_gss_mech_publish(output, &native_output);
  return status;
}

/** C89 facade contract for cpkt_gss_export_credential; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status
cpkt_gss_export_credential(cpkt_gss_status *minor_out,
                           const cpkt_gss_credential *credential,
                           cpkt_gss_buffer *token_out) {
  gss_buffer_desc native = {0, NULL};
  OM_uint32 minor;
  OM_uint32 status =
      gss_export_cred(&minor, (gss_cred_id_t)credential, &native);
  cpkt_gss_mech_publish(token_out, &native);
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_import_credential; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status
cpkt_gss_import_credential(cpkt_gss_status *minor_out,
                           const cpkt_gss_buffer *token,
                           cpkt_gss_credential **credential_out) {
  gss_buffer_desc native_token = cpkt_gss_mech_buffer(token);
  gss_cred_id_t native = GSS_C_NO_CREDENTIAL;
  OM_uint32 minor, status;
  if (credential_out == NULL)
    return cpkt_gss_mech_bad_argument(minor_out);
  *credential_out = NULL;
  status = gss_import_cred(&minor, &native_token, &native);
  *credential_out = (cpkt_gss_credential *)native;
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_principal_name_to_uid; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_principal_name_to_uid(cpkt_gss_status *minor_out,
                                               const cpkt_gss_name *name,
                                               const cpkt_gss_oid *mechanism,
                                               unsigned long *uid_out) {
  uid_t uid = 0;
  OM_uint32 minor, status;
  if (uid_out == NULL)
    return cpkt_gss_mech_bad_argument(minor_out);
  *uid_out = 0;
  status = gss_pname_to_uid(&minor, (gss_name_t)name, (gss_OID)mechanism, &uid);
  *uid_out = (unsigned long)uid;
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_mechanism_invoke; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_mechanism_invoke(cpkt_gss_status *minor_out,
                                          const cpkt_gss_oid *mechanism,
                                          const cpkt_gss_oid *operation,
                                          cpkt_gss_buffer *value_in_out) {
  gss_buffer_desc native = cpkt_gss_mech_buffer(value_in_out);
  OM_uint32 minor, status;
  if (value_in_out == NULL)
    return cpkt_gss_mech_bad_argument(minor_out);
  status = gssspi_mech_invoke(&minor, (gss_OID)mechanism, (gss_OID)operation,
                              &native);
  value_in_out->length = native.length;
  value_in_out->value = native.value;
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_seal; see the public header for ownership
 * and callback lifetime. */
cpkt_gss_status cpkt_gss_seal(cpkt_gss_status *minor_out,
                              const cpkt_gss_context *context, int conf,
                              int qop, const cpkt_gss_buffer *input,
                              int *conf_out, cpkt_gss_buffer *output) {
  gss_buffer_desc native_input = cpkt_gss_mech_buffer(input);
  gss_buffer_desc native_output = {0, NULL};
  int native_conf = 0;
  OM_uint32 minor;
  OM_uint32 status = gss_seal(&minor, (gss_ctx_id_t)context, conf, qop,
                              &native_input, &native_conf, &native_output);
  if (conf_out != NULL)
    *conf_out = native_conf;
  cpkt_gss_mech_publish(output, &native_output);
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_unseal; see the public header for ownership
 * and callback lifetime. */
cpkt_gss_status cpkt_gss_unseal(cpkt_gss_status *minor_out,
                                const cpkt_gss_context *context,
                                const cpkt_gss_buffer *input,
                                cpkt_gss_buffer *output, int *conf_out,
                                int *qop_out) {
  gss_buffer_desc native_input = cpkt_gss_mech_buffer(input);
  gss_buffer_desc native_output = {0, NULL};
  int native_conf = 0, native_qop = 0;
  OM_uint32 minor;
  OM_uint32 status = gss_unseal(&minor, (gss_ctx_id_t)context, &native_input,
                                &native_output, &native_conf, &native_qop);
  if (conf_out != NULL)
    *conf_out = native_conf;
  if (qop_out != NULL)
    *qop_out = native_qop;
  cpkt_gss_mech_publish(output, &native_output);
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_sign; see the public header for ownership
 * and callback lifetime. */
cpkt_gss_status cpkt_gss_sign(cpkt_gss_status *minor_out,
                              const cpkt_gss_context *context, int qop,
                              const cpkt_gss_buffer *message,
                              cpkt_gss_buffer *token_out) {
  gss_buffer_desc native_message = cpkt_gss_mech_buffer(message);
  gss_buffer_desc native_token = {0, NULL};
  OM_uint32 minor;
  OM_uint32 status = gss_sign(&minor, (gss_ctx_id_t)context, qop,
                              &native_message, &native_token);
  cpkt_gss_mech_publish(token_out, &native_token);
  return cpkt_gss_mech_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_verify; see the public header for ownership
 * and callback lifetime. */
cpkt_gss_status cpkt_gss_verify(cpkt_gss_status *minor_out,
                                const cpkt_gss_context *context,
                                const cpkt_gss_buffer *message,
                                const cpkt_gss_buffer *token, int *qop_out) {
  gss_buffer_desc native_message = cpkt_gss_mech_buffer(message);
  gss_buffer_desc native_token = cpkt_gss_mech_buffer(token);
  int native_qop = 0;
  OM_uint32 minor;
  OM_uint32 status = gss_verify(&minor, (gss_ctx_id_t)context, &native_message,
                                &native_token, &native_qop);
  if (qop_out != NULL)
    *qop_out = native_qop;
  return cpkt_gss_mech_result(status, minor, minor_out);
}
