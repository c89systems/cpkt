#include <cpkt/gssapi.h>

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>

#include <gssapi/gssapi_ext.h>

typedef char
    cpkt_gss_long_matches_ssize_t[(sizeof(long) == sizeof(ssize_t)) ? 1 : -1];

static cpkt_gss_status cpkt_gss_ext_finish(OM_uint32 status, OM_uint32 minor,
                                           cpkt_gss_status *minor_out) {
  if (minor_out != NULL)
    *minor_out = minor;
  return status;
}

static cpkt_gss_status cpkt_gss_ext_bad_parameter(cpkt_gss_status *minor_out) {
  return cpkt_gss_ext_finish(GSS_S_CALL_BAD_STRUCTURE, EINVAL, minor_out);
}

static gss_buffer_desc cpkt_gss_ext_buffer(const cpkt_gss_buffer *public_view) {
  gss_buffer_desc native;
  native.length = public_view == NULL ? 0 : public_view->length;
  native.value = public_view == NULL ? NULL : public_view->value;
  return native;
}

static void cpkt_gss_ext_publish_buffer(cpkt_gss_buffer *public_view,
                                        gss_buffer_desc *native) {
  OM_uint32 ignored;
  if (public_view == NULL) {
    if (native->value != NULL)
      (void)gss_release_buffer(&ignored, native);
  } else {
    public_view->length = native->length;
    public_view->value = native->value;
  }
}

/** C89 facade contract for cpkt_gss_inquire_mechanisms_for_name; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status
cpkt_gss_inquire_mechanisms_for_name(cpkt_gss_status *minor_out,
                                     const cpkt_gss_name *name,
                                     cpkt_gss_oid_set **mechanisms_out) {
  OM_uint32 minor;
  gss_OID_set native = GSS_C_NO_OID_SET;
  OM_uint32 status;
  if (mechanisms_out == NULL)
    return cpkt_gss_ext_bad_parameter(minor_out);
  *mechanisms_out = NULL;
  status = gss_inquire_mechs_for_name(&minor, (gss_name_t)name, &native);
  *mechanisms_out = (cpkt_gss_oid_set *)native;
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_inquire_names_for_mechanism; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status
cpkt_gss_inquire_names_for_mechanism(cpkt_gss_status *minor_out,
                                     const cpkt_gss_oid *mechanism,
                                     cpkt_gss_oid_set **name_types_out) {
  OM_uint32 minor;
  gss_OID_set native = GSS_C_NO_OID_SET;
  OM_uint32 status;
  if (name_types_out == NULL)
    return cpkt_gss_ext_bad_parameter(minor_out);
  *name_types_out = NULL;
  status = gss_inquire_names_for_mech(&minor, (gss_OID)mechanism, &native);
  *name_types_out = (cpkt_gss_oid_set *)native;
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_display_name_ext; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_display_name_ext(cpkt_gss_status *minor_out,
                                          const cpkt_gss_name *name,
                                          const cpkt_gss_oid *display_type,
                                          cpkt_gss_buffer *output) {
  OM_uint32 minor;
  gss_buffer_desc native = {0, NULL};
  OM_uint32 status = gss_display_name_ext(&minor, (gss_name_t)name,
                                          (gss_OID)display_type, &native);
  cpkt_gss_ext_publish_buffer(output, &native);
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_export_name_composite; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_export_name_composite(cpkt_gss_status *minor_out,
                                               const cpkt_gss_name *name,
                                               cpkt_gss_buffer *output) {
  OM_uint32 minor;
  gss_buffer_desc native = {0, NULL};
  OM_uint32 status =
      gss_export_name_composite(&minor, (gss_name_t)name, &native);
  cpkt_gss_ext_publish_buffer(output, &native);
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_inquire_name; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_inquire_name(cpkt_gss_status *minor_out,
                                      const cpkt_gss_name *name,
                                      int *is_mechanism_name_out,
                                      const cpkt_gss_oid **mechanism_out,
                                      cpkt_gss_buffer_set **attributes_out) {
  OM_uint32 minor, ignored;
  gss_OID mechanism = GSS_C_NO_OID;
  gss_buffer_set_t attributes = GSS_C_NO_BUFFER_SET;
  int is_mechanism_name = 0;
  OM_uint32 status = gss_inquire_name(
      &minor, (gss_name_t)name, &is_mechanism_name, &mechanism, &attributes);
  if (is_mechanism_name_out != NULL)
    *is_mechanism_name_out = is_mechanism_name;
  if (mechanism_out != NULL)
    *mechanism_out = (const cpkt_gss_oid *)mechanism;
  if (attributes_out != NULL)
    *attributes_out = (cpkt_gss_buffer_set *)attributes;
  else if (attributes != GSS_C_NO_BUFFER_SET)
    (void)gss_release_buffer_set(&ignored, &attributes);
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_get_name_attribute; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_get_name_attribute(
    cpkt_gss_status *minor_out, const cpkt_gss_name *name,
    const cpkt_gss_buffer *attribute, int *authenticated_out, int *complete_out,
    cpkt_gss_buffer *value_out, cpkt_gss_buffer *display_value_out,
    int *more_in_out) {
  OM_uint32 minor;
  gss_buffer_desc native_attribute = cpkt_gss_ext_buffer(attribute);
  gss_buffer_desc value = {0, NULL}, display_value = {0, NULL};
  int authenticated = 0, complete = 0,
      more = more_in_out == NULL ? 0 : *more_in_out;
  OM_uint32 status = gss_get_name_attribute(
      &minor, (gss_name_t)name, &native_attribute, &authenticated, &complete,
      &value, &display_value, &more);
  if (authenticated_out != NULL)
    *authenticated_out = authenticated;
  if (complete_out != NULL)
    *complete_out = complete;
  if (more_in_out != NULL)
    *more_in_out = more;
  cpkt_gss_ext_publish_buffer(value_out, &value);
  cpkt_gss_ext_publish_buffer(display_value_out, &display_value);
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_set_name_attribute; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_set_name_attribute(cpkt_gss_status *minor_out,
                                            const cpkt_gss_name *name,
                                            int complete,
                                            const cpkt_gss_buffer *attribute,
                                            const cpkt_gss_buffer *value) {
  OM_uint32 minor;
  gss_buffer_desc native_attribute = cpkt_gss_ext_buffer(attribute);
  gss_buffer_desc native_value = cpkt_gss_ext_buffer(value);
  OM_uint32 status = gss_set_name_attribute(&minor, (gss_name_t)name, complete,
                                            &native_attribute, &native_value);
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_delete_name_attribute; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status
cpkt_gss_delete_name_attribute(cpkt_gss_status *minor_out,
                               const cpkt_gss_name *name,
                               const cpkt_gss_buffer *attribute) {
  OM_uint32 minor;
  gss_buffer_desc native_attribute = cpkt_gss_ext_buffer(attribute);
  OM_uint32 status =
      gss_delete_name_attribute(&minor, (gss_name_t)name, &native_attribute);
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_local_name; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_local_name(cpkt_gss_status *minor_out,
                                    const cpkt_gss_name *name,
                                    const cpkt_gss_oid *mechanism,
                                    cpkt_gss_buffer *local_name_out) {
  OM_uint32 minor;
  gss_buffer_desc native = {0, NULL};
  OM_uint32 status =
      gss_localname(&minor, (gss_name_t)name, (gss_OID)mechanism, &native);
  cpkt_gss_ext_publish_buffer(local_name_out, &native);
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_authorize_local_name; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_authorize_local_name(cpkt_gss_status *minor_out,
                                              const cpkt_gss_name *name,
                                              const cpkt_gss_name *local_user) {
  OM_uint32 minor;
  OM_uint32 status =
      gss_authorize_localname(&minor, (gss_name_t)name, (gss_name_t)local_user);
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_user_ok; see the public header for
 * ownership and callback lifetime. */
int cpkt_gss_user_ok(const cpkt_gss_name *name, const char *user) {
  return gss_userok((gss_name_t)name, user);
}

/** C89 facade contract for cpkt_gss_inquire_context; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_inquire_context(
    cpkt_gss_status *minor_out, const cpkt_gss_context *context,
    cpkt_gss_name **initiator_out, cpkt_gss_name **acceptor_out,
    cpkt_gss_lifetime *lifetime_out, const cpkt_gss_oid **mechanism_out,
    cpkt_gss_flags *flags_out, int *locally_initiated_out, int *open_out) {
  OM_uint32 minor, ignored;
  gss_name_t initiator = GSS_C_NO_NAME, acceptor = GSS_C_NO_NAME;
  gss_OID mechanism = GSS_C_NO_OID;
  OM_uint32 lifetime = 0, flags = 0;
  int locally_initiated = 0, open = 0;
  OM_uint32 status = gss_inquire_context(
      &minor, (gss_ctx_id_t)context, &initiator, &acceptor, &lifetime,
      &mechanism, &flags, &locally_initiated, &open);
  if (initiator_out != NULL)
    *initiator_out = (cpkt_gss_name *)initiator;
  else if (initiator != GSS_C_NO_NAME)
    (void)gss_release_name(&ignored, &initiator);
  if (acceptor_out != NULL)
    *acceptor_out = (cpkt_gss_name *)acceptor;
  else if (acceptor != GSS_C_NO_NAME)
    (void)gss_release_name(&ignored, &acceptor);
  if (lifetime_out != NULL)
    *lifetime_out = lifetime;
  if (mechanism_out != NULL)
    *mechanism_out = (const cpkt_gss_oid *)mechanism;
  if (flags_out != NULL)
    *flags_out = flags;
  if (locally_initiated_out != NULL)
    *locally_initiated_out = locally_initiated;
  if (open_out != NULL)
    *open_out = open;
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_export_context; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_export_context(cpkt_gss_status *minor_out,
                                        cpkt_gss_context **context,
                                        cpkt_gss_buffer *token_out) {
  OM_uint32 minor;
  gss_ctx_id_t native_context;
  gss_buffer_desc native_token = {0, NULL};
  OM_uint32 status;
  if (context == NULL)
    return cpkt_gss_ext_bad_parameter(minor_out);
  native_context = (gss_ctx_id_t)*context;
  status = gss_export_sec_context(&minor, &native_context, &native_token);
  *context = (cpkt_gss_context *)native_context;
  cpkt_gss_ext_publish_buffer(token_out, &native_token);
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_import_context; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_import_context(cpkt_gss_status *minor_out,
                                        const cpkt_gss_buffer *token,
                                        cpkt_gss_context **context_out) {
  OM_uint32 minor;
  gss_buffer_desc native_token = cpkt_gss_ext_buffer(token);
  gss_ctx_id_t native = GSS_C_NO_CONTEXT;
  OM_uint32 status;
  if (context_out == NULL)
    return cpkt_gss_ext_bad_parameter(minor_out);
  *context_out = NULL;
  status = gss_import_sec_context(&minor, &native_token, &native);
  *context_out = (cpkt_gss_context *)native;
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_process_context_token; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_process_context_token(cpkt_gss_status *minor_out,
                                               const cpkt_gss_context *context,
                                               const cpkt_gss_buffer *token) {
  OM_uint32 minor;
  gss_buffer_desc native_token = cpkt_gss_ext_buffer(token);
  OM_uint32 status =
      gss_process_context_token(&minor, (gss_ctx_id_t)context, &native_token);
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_complete_auth_token; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_complete_auth_token(cpkt_gss_status *minor_out,
                                             const cpkt_gss_context *context,
                                             const cpkt_gss_buffer *token) {
  OM_uint32 minor;
  gss_buffer_desc native_token = cpkt_gss_ext_buffer(token);
  OM_uint32 status =
      gss_complete_auth_token(&minor, (gss_ctx_id_t)context, &native_token);
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_wrap_size_limit; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_wrap_size_limit(cpkt_gss_status *minor_out,
                                         const cpkt_gss_context *context,
                                         int conf, cpkt_gss_qop qop,
                                         unsigned long output_size,
                                         unsigned long *maximum_input_out) {
  OM_uint32 minor;
  OM_uint32 maximum_input = 0;
  OM_uint32 status;
  if (output_size > 0xffffffffUL || qop > 0xffffffffUL)
    return cpkt_gss_ext_bad_parameter(minor_out);
  status =
      gss_wrap_size_limit(&minor, (gss_ctx_id_t)context, conf, (gss_qop_t)qop,
                          (OM_uint32)output_size, &maximum_input);
  if (maximum_input_out != NULL)
    *maximum_input_out = maximum_input;
  return cpkt_gss_ext_finish(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_pseudo_random; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_pseudo_random(cpkt_gss_status *minor_out,
                                       const cpkt_gss_context *context,
                                       int key_selector,
                                       const cpkt_gss_buffer *input,
                                       long output_byte_count,
                                       cpkt_gss_buffer *output) {
  OM_uint32 minor;
  gss_buffer_desc native_input = cpkt_gss_ext_buffer(input);
  gss_buffer_desc native_output = {0, NULL};
  OM_uint32 status = gss_pseudo_random(
      &minor, (gss_ctx_id_t)context, key_selector, &native_input,
      (ssize_t)output_byte_count, &native_output);
  cpkt_gss_ext_publish_buffer(output, &native_output);
  return cpkt_gss_ext_finish(status, minor, minor_out);
}
