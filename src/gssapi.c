#include <cpkt/gssapi.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <gssapi/gssapi.h>

typedef char cpkt_gss_status_fits_public_type
    [(sizeof(OM_uint32) <= sizeof(unsigned long)) ? 1 : -1];
typedef char
    cpkt_gss_status_is_32_bits[(sizeof(OM_uint32) * CHAR_BIT == 32) ? 1 : -1];

static OM_uint32 cpkt_gss_status_input(cpkt_gss_status value) {
  return (OM_uint32)value;
}

static int cpkt_gss_u32_fits(unsigned long value) {
  return value <= 0xffffffffUL;
}

static cpkt_gss_status cpkt_gss_invalid_u32(cpkt_gss_status *minor_out) {
  if (minor_out != NULL)
    *minor_out = EINVAL;
  return GSS_S_CALL_BAD_STRUCTURE;
}

static cpkt_gss_status
cpkt_gss_missing_handle_output(cpkt_gss_status *minor_out) {
  if (minor_out != NULL)
    *minor_out = EINVAL;
  return GSS_S_CALL_BAD_STRUCTURE;
}

static int cpkt_gss_bindings_fit(const cpkt_gss_channel_bindings *bindings) {
  return bindings == NULL ||
         (cpkt_gss_u32_fits(bindings->initiator_address_type) &&
          cpkt_gss_u32_fits(bindings->acceptor_address_type));
}

static cpkt_gss_status cpkt_gss_finish(OM_uint32 status, OM_uint32 minor,
                                       cpkt_gss_status *minor_out) {
  if (minor_out != NULL) {
    *minor_out = (cpkt_gss_status)minor;
  }
  return (cpkt_gss_status)status;
}

static gss_buffer_desc cpkt_gss_native_buffer(const cpkt_gss_buffer *buffer) {
  gss_buffer_desc native;
  native.length = buffer == NULL ? 0 : buffer->length;
  native.value = buffer == NULL ? NULL : buffer->value;
  return native;
}

static void cpkt_gss_public_buffer(cpkt_gss_buffer *target,
                                   const gss_buffer_desc *source) {
  if (target != NULL) {
    target->length = source == NULL ? 0 : source->length;
    target->value = source == NULL ? NULL : source->value;
  }
}

static void cpkt_gss_publish_buffer(cpkt_gss_buffer *target,
                                    gss_buffer_desc *source) {
  OM_uint32 ignored_minor;
  if (target != NULL) {
    cpkt_gss_public_buffer(target, source);
  } else if (source != NULL && source->value != NULL) {
    (void)gss_release_buffer(&ignored_minor, source);
  }
}

static struct gss_channel_bindings_struct
cpkt_gss_native_bindings(const cpkt_gss_channel_bindings *bindings) {
  struct gss_channel_bindings_struct native;
  memset(&native, 0, sizeof(native));
  if (bindings != NULL) {
    native.initiator_addrtype = (OM_uint32)bindings->initiator_address_type;
    native.initiator_address =
        cpkt_gss_native_buffer(&bindings->initiator_address);
    native.acceptor_addrtype = (OM_uint32)bindings->acceptor_address_type;
    native.acceptor_address =
        cpkt_gss_native_buffer(&bindings->acceptor_address);
    native.application_data =
        cpkt_gss_native_buffer(&bindings->application_data);
  }
  return native;
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_status_is_error. */
int cpkt_gss_status_is_error(cpkt_gss_status status) {
  return !cpkt_gss_u32_fits(status) ||
         GSS_ERROR(cpkt_gss_status_input(status)) != 0;
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_name_type_user. */
const cpkt_gss_oid *cpkt_gss_name_type_user(void) {
  return (const cpkt_gss_oid *)GSS_C_NT_USER_NAME;
}
/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_name_type_machine_uid. */
const cpkt_gss_oid *cpkt_gss_name_type_machine_uid(void) {
  return (const cpkt_gss_oid *)GSS_C_NT_MACHINE_UID_NAME;
}
/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_name_type_string_uid. */
const cpkt_gss_oid *cpkt_gss_name_type_string_uid(void) {
  return (const cpkt_gss_oid *)GSS_C_NT_STRING_UID_NAME;
}
/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_name_type_hostbased_service. */
const cpkt_gss_oid *cpkt_gss_name_type_hostbased_service(void) {
  return (const cpkt_gss_oid *)GSS_C_NT_HOSTBASED_SERVICE;
}
/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_name_type_anonymous. */
const cpkt_gss_oid *cpkt_gss_name_type_anonymous(void) {
  return (const cpkt_gss_oid *)GSS_C_NT_ANONYMOUS;
}
/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_name_type_exported_name. */
const cpkt_gss_oid *cpkt_gss_name_type_exported_name(void) {
  return (const cpkt_gss_oid *)GSS_C_NT_EXPORT_NAME;
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_release_buffer. */
cpkt_gss_status cpkt_gss_release_buffer(cpkt_gss_status *minor_out,
                                        cpkt_gss_buffer *buffer) {
  OM_uint32 minor;
  gss_buffer_desc native;
  OM_uint32 status;
  native = cpkt_gss_native_buffer(buffer);
  status = gss_release_buffer(&minor, &native);
  cpkt_gss_public_buffer(buffer, &native);
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_release_oid. */
cpkt_gss_status cpkt_gss_release_oid(cpkt_gss_status *minor_out,
                                     cpkt_gss_oid **oid) {
  OM_uint32 minor;
  gss_OID native = oid == NULL ? GSS_C_NO_OID : (gss_OID)*oid;
  OM_uint32 status = gss_release_oid(&minor, &native);
  if (oid != NULL)
    *oid = (cpkt_gss_oid *)native;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_release_oid_set. */
cpkt_gss_status cpkt_gss_release_oid_set(cpkt_gss_status *minor_out,
                                         cpkt_gss_oid_set **set) {
  OM_uint32 minor;
  gss_OID_set native = set == NULL ? GSS_C_NO_OID_SET : (gss_OID_set)*set;
  OM_uint32 status = gss_release_oid_set(&minor, &native);
  if (set != NULL)
    *set = (cpkt_gss_oid_set *)native;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_release_name. */
cpkt_gss_status cpkt_gss_release_name(cpkt_gss_status *minor_out,
                                      cpkt_gss_name **name) {
  OM_uint32 minor;
  gss_name_t native = name == NULL ? GSS_C_NO_NAME : (gss_name_t)*name;
  OM_uint32 status = gss_release_name(&minor, &native);
  if (name != NULL)
    *name = (cpkt_gss_name *)native;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_release_credential. */
cpkt_gss_status cpkt_gss_release_credential(cpkt_gss_status *minor_out,
                                            cpkt_gss_credential **credential) {
  OM_uint32 minor;
  gss_cred_id_t native =
      credential == NULL ? GSS_C_NO_CREDENTIAL : (gss_cred_id_t)*credential;
  OM_uint32 status = gss_release_cred(&minor, &native);
  if (credential != NULL)
    *credential = (cpkt_gss_credential *)native;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_delete_context. */
cpkt_gss_status cpkt_gss_delete_context(cpkt_gss_status *minor_out,
                                        cpkt_gss_context **context,
                                        cpkt_gss_buffer *token) {
  OM_uint32 minor;
  gss_ctx_id_t native_context =
      context == NULL ? GSS_C_NO_CONTEXT : (gss_ctx_id_t)*context;
  gss_buffer_desc native_token = cpkt_gss_native_buffer(token);
  OM_uint32 status =
      gss_delete_sec_context(&minor, &native_context, &native_token);
  if (context != NULL)
    *context = (cpkt_gss_context *)native_context;
  cpkt_gss_publish_buffer(token, &native_token);
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_indicate_mechanisms. */
cpkt_gss_status cpkt_gss_indicate_mechanisms(cpkt_gss_status *minor_out,
                                             cpkt_gss_oid_set **set_out) {
  OM_uint32 minor;
  gss_OID_set set = GSS_C_NO_OID_SET;
  OM_uint32 status;
  if (set_out == NULL)
    return cpkt_gss_missing_handle_output(minor_out);
  status = gss_indicate_mechs(&minor, &set);
  *set_out = (cpkt_gss_oid_set *)set;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_oid_set_count. */
size_t cpkt_gss_oid_set_count(const cpkt_gss_oid_set *set) {
  return set == NULL ? 0 : ((const gss_OID_set)set)->count;
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_oid_set_at. */
const cpkt_gss_oid *cpkt_gss_oid_set_at(const cpkt_gss_oid_set *set,
                                        size_t index) {
  const gss_OID_set native = (const gss_OID_set)set;
  return native == NULL || index >= native->count
             ? NULL
             : (const cpkt_gss_oid *)&native->elements[index];
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_create_oid_set. */
cpkt_gss_status cpkt_gss_create_oid_set(cpkt_gss_status *minor_out,
                                        cpkt_gss_oid_set **set_out) {
  OM_uint32 minor;
  gss_OID_set set = GSS_C_NO_OID_SET;
  OM_uint32 status;
  if (set_out == NULL)
    return cpkt_gss_missing_handle_output(minor_out);
  status = gss_create_empty_oid_set(&minor, &set);
  *set_out = (cpkt_gss_oid_set *)set;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_add_oid_to_set. */
cpkt_gss_status cpkt_gss_add_oid_to_set(cpkt_gss_status *minor_out,
                                        const cpkt_gss_oid *oid,
                                        cpkt_gss_oid_set **set) {
  OM_uint32 minor;
  gss_OID_set native = set == NULL ? GSS_C_NO_OID_SET : (gss_OID_set)*set;
  OM_uint32 status;
  if (set == NULL)
    return cpkt_gss_missing_handle_output(minor_out);
  status = gss_add_oid_set_member(&minor, (gss_OID)oid, &native);
  *set = (cpkt_gss_oid_set *)native;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_oid_set_contains. */
cpkt_gss_status cpkt_gss_oid_set_contains(cpkt_gss_status *minor_out,
                                          const cpkt_gss_oid *oid,
                                          const cpkt_gss_oid_set *set,
                                          int *present_out) {
  OM_uint32 minor;
  int present = 0;
  OM_uint32 status =
      gss_test_oid_set_member(&minor, (gss_OID)oid, (gss_OID_set)set, &present);
  if (present_out != NULL)
    *present_out = present;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_oid_from_text. */
cpkt_gss_status cpkt_gss_oid_from_text(cpkt_gss_status *minor_out,
                                       const cpkt_gss_buffer *text,
                                       cpkt_gss_oid **oid_out) {
  OM_uint32 minor;
  gss_buffer_desc native_text = cpkt_gss_native_buffer(text);
  gss_OID oid = GSS_C_NO_OID;
  OM_uint32 status;
  if (oid_out == NULL)
    return cpkt_gss_missing_handle_output(minor_out);
  status = gss_str_to_oid(&minor, &native_text, &oid);
  *oid_out = (cpkt_gss_oid *)oid;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Copies a raw DER OID descriptor into an owned handle. On invalid input
 * the output is cleared; release a successful handle with cpkt_gss_oid_free. */
cpkt_gss_status cpkt_gss_oid_from_bytes(cpkt_gss_status *minor_out,
                                        const void *bytes, size_t length,
                                        cpkt_gss_oid **oid_out) {
  gss_OID native;
  if (oid_out == NULL || length > UINT_MAX || (length != 0 && bytes == NULL)) {
    if (oid_out != NULL)
      *oid_out = NULL;
    return cpkt_gss_finish(GSS_S_CALL_BAD_STRUCTURE, EINVAL, minor_out);
  }
  *oid_out = NULL;
  native = (gss_OID)calloc(1, sizeof(*native));
  if (native == NULL)
    return cpkt_gss_finish(GSS_S_FAILURE, ENOMEM, minor_out);
  if (length != 0) {
    native->elements = malloc(length);
    if (native->elements == NULL) {
      free(native);
      return cpkt_gss_finish(GSS_S_FAILURE, ENOMEM, minor_out);
    }
    memcpy(native->elements, bytes, length);
  }
  native->length = (OM_uint32)length;
  *oid_out = (cpkt_gss_oid *)native;
  return cpkt_gss_finish(GSS_S_COMPLETE, 0, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_oid_to_text. */
cpkt_gss_status cpkt_gss_oid_to_text(cpkt_gss_status *minor_out,
                                     const cpkt_gss_oid *oid,
                                     cpkt_gss_buffer *text_out) {
  OM_uint32 minor;
  gss_buffer_desc text;
  OM_uint32 status;
  memset(&text, 0, sizeof(text));
  status = gss_oid_to_str(&minor, (gss_OID)oid, &text);
  cpkt_gss_publish_buffer(text_out, &text);
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_import_name. */
cpkt_gss_status cpkt_gss_import_name(cpkt_gss_status *minor_out,
                                     const cpkt_gss_buffer *input,
                                     const cpkt_gss_oid *type,
                                     cpkt_gss_name **name_out) {
  OM_uint32 minor;
  gss_buffer_desc native_input = cpkt_gss_native_buffer(input);
  gss_name_t name = GSS_C_NO_NAME;
  OM_uint32 status;
  if (name_out == NULL)
    return cpkt_gss_missing_handle_output(minor_out);
  status = gss_import_name(&minor, &native_input, (gss_OID)type, &name);
  *name_out = (cpkt_gss_name *)name;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_display_name. */
cpkt_gss_status cpkt_gss_display_name(cpkt_gss_status *minor_out,
                                      const cpkt_gss_name *name,
                                      cpkt_gss_buffer *text_out,
                                      const cpkt_gss_oid **type_out) {
  OM_uint32 minor;
  gss_buffer_desc text;
  gss_OID type = GSS_C_NO_OID;
  OM_uint32 status;
  memset(&text, 0, sizeof(text));
  status = gss_display_name(&minor, (gss_name_t)name, &text, &type);
  cpkt_gss_publish_buffer(text_out, &text);
  if (type_out != NULL)
    *type_out = (const cpkt_gss_oid *)type;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_compare_names. */
cpkt_gss_status cpkt_gss_compare_names(cpkt_gss_status *minor_out,
                                       const cpkt_gss_name *left,
                                       const cpkt_gss_name *right,
                                       int *equal_out) {
  OM_uint32 minor;
  int equal = 0;
  OM_uint32 status =
      gss_compare_name(&minor, (gss_name_t)left, (gss_name_t)right, &equal);
  if (equal_out != NULL)
    *equal_out = equal;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_duplicate_name. */
cpkt_gss_status cpkt_gss_duplicate_name(cpkt_gss_status *minor_out,
                                        const cpkt_gss_name *source,
                                        cpkt_gss_name **copy_out) {
  OM_uint32 minor;
  gss_name_t copy = GSS_C_NO_NAME;
  OM_uint32 status;
  if (copy_out == NULL)
    return cpkt_gss_missing_handle_output(minor_out);
  status = gss_duplicate_name(&minor, (gss_name_t)source, &copy);
  *copy_out = (cpkt_gss_name *)copy;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_canonicalize_name. */
cpkt_gss_status cpkt_gss_canonicalize_name(cpkt_gss_status *minor_out,
                                           const cpkt_gss_name *source,
                                           const cpkt_gss_oid *mechanism,
                                           cpkt_gss_name **canonical_out) {
  OM_uint32 minor;
  gss_name_t canonical = GSS_C_NO_NAME;
  OM_uint32 status;
  if (canonical_out == NULL)
    return cpkt_gss_missing_handle_output(minor_out);
  status = gss_canonicalize_name(&minor, (gss_name_t)source, (gss_OID)mechanism,
                                 &canonical);
  *canonical_out = (cpkt_gss_name *)canonical;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_export_name. */
cpkt_gss_status cpkt_gss_export_name(cpkt_gss_status *minor_out,
                                     const cpkt_gss_name *name,
                                     cpkt_gss_buffer *token_out) {
  OM_uint32 minor;
  gss_buffer_desc token;
  OM_uint32 status;
  memset(&token, 0, sizeof(token));
  status = gss_export_name(&minor, (gss_name_t)name, &token);
  cpkt_gss_publish_buffer(token_out, &token);
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_acquire_credential. */
cpkt_gss_status cpkt_gss_acquire_credential(
    cpkt_gss_status *minor_out, const cpkt_gss_name *name,
    cpkt_gss_lifetime requested_lifetime, const cpkt_gss_oid_set *desired,
    int usage, cpkt_gss_credential **credential_out,
    cpkt_gss_oid_set **actual_out, cpkt_gss_lifetime *lifetime_out) {
  OM_uint32 minor, lifetime = 0;
  gss_cred_id_t credential = GSS_C_NO_CREDENTIAL;
  gss_OID_set actual = GSS_C_NO_OID_SET;
  OM_uint32 ignored;
  OM_uint32 status;
  if (!cpkt_gss_u32_fits(requested_lifetime))
    return cpkt_gss_invalid_u32(minor_out);
  status =
      gss_acquire_cred(&minor, (gss_name_t)name, (OM_uint32)requested_lifetime,
                       (gss_OID_set)desired, usage, &credential,
                       actual_out == NULL ? NULL : &actual, &lifetime);
  if (credential_out != NULL)
    *credential_out = (cpkt_gss_credential *)credential;
  else if (credential != GSS_C_NO_CREDENTIAL)
    (void)gss_release_cred(&ignored, &credential);
  if (actual_out != NULL)
    *actual_out = (cpkt_gss_oid_set *)actual;
  if (lifetime_out != NULL)
    *lifetime_out = (cpkt_gss_lifetime)lifetime;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_inquire_credential. */
cpkt_gss_status cpkt_gss_inquire_credential(
    cpkt_gss_status *minor_out, const cpkt_gss_credential *credential,
    cpkt_gss_name **name_out, cpkt_gss_lifetime *lifetime_out, int *usage_out,
    cpkt_gss_oid_set **mechanisms_out) {
  OM_uint32 minor, lifetime = 0;
  gss_name_t name = GSS_C_NO_NAME;
  gss_OID_set mechanisms = GSS_C_NO_OID_SET;
  int usage = 0;
  OM_uint32 status = gss_inquire_cred(
      &minor, (gss_cred_id_t)credential, name_out == NULL ? NULL : &name,
      &lifetime, &usage, mechanisms_out == NULL ? NULL : &mechanisms);
  if (name_out != NULL)
    *name_out = (cpkt_gss_name *)name;
  if (lifetime_out != NULL)
    *lifetime_out = (cpkt_gss_lifetime)lifetime;
  if (usage_out != NULL)
    *usage_out = usage;
  if (mechanisms_out != NULL)
    *mechanisms_out = (cpkt_gss_oid_set *)mechanisms;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_init_context. */
cpkt_gss_status cpkt_gss_init_context(
    cpkt_gss_status *minor_out, const cpkt_gss_credential *credential,
    cpkt_gss_context **context, const cpkt_gss_name *target,
    const cpkt_gss_oid *mechanism, cpkt_gss_flags flags,
    cpkt_gss_lifetime lifetime, const cpkt_gss_channel_bindings *bindings,
    const cpkt_gss_buffer *input, const cpkt_gss_oid **actual_out,
    cpkt_gss_buffer *output, cpkt_gss_flags *returned_flags_out,
    cpkt_gss_lifetime *lifetime_out) {
  OM_uint32 minor, returned_flags = 0, returned_lifetime = 0;
  gss_ctx_id_t native_context =
      context == NULL ? GSS_C_NO_CONTEXT : (gss_ctx_id_t)*context;
  struct gss_channel_bindings_struct native_bindings =
      cpkt_gss_native_bindings(bindings);
  gss_buffer_desc native_input = cpkt_gss_native_buffer(input), native_output;
  gss_OID actual = GSS_C_NO_OID;
  OM_uint32 status;
  if (context == NULL)
    return cpkt_gss_missing_handle_output(minor_out);
  if (!cpkt_gss_u32_fits(flags) || !cpkt_gss_u32_fits(lifetime) ||
      !cpkt_gss_bindings_fit(bindings))
    return cpkt_gss_invalid_u32(minor_out);
  memset(&native_output, 0, sizeof(native_output));
  status = gss_init_sec_context(
      &minor, (gss_cred_id_t)credential, &native_context, (gss_name_t)target,
      (gss_OID)mechanism, (OM_uint32)flags, (OM_uint32)lifetime,
      bindings == NULL ? GSS_C_NO_CHANNEL_BINDINGS : &native_bindings,
      input == NULL ? GSS_C_NO_BUFFER : &native_input, &actual, &native_output,
      &returned_flags, &returned_lifetime);
  if (context != NULL)
    *context = (cpkt_gss_context *)native_context;
  if (actual_out != NULL)
    *actual_out = (const cpkt_gss_oid *)actual;
  cpkt_gss_publish_buffer(output, &native_output);
  if (returned_flags_out != NULL)
    *returned_flags_out = (cpkt_gss_flags)returned_flags;
  if (lifetime_out != NULL)
    *lifetime_out = (cpkt_gss_lifetime)returned_lifetime;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_accept_context. */
cpkt_gss_status cpkt_gss_accept_context(
    cpkt_gss_status *minor_out, cpkt_gss_context **context,
    const cpkt_gss_credential *credential, const cpkt_gss_buffer *input,
    const cpkt_gss_channel_bindings *bindings, cpkt_gss_name **source_out,
    const cpkt_gss_oid **mechanism_out, cpkt_gss_buffer *output,
    cpkt_gss_flags *returned_flags_out, cpkt_gss_lifetime *lifetime_out,
    cpkt_gss_credential **delegated_out) {
  OM_uint32 minor, returned_flags = 0, returned_lifetime = 0;
  gss_ctx_id_t native_context =
      context == NULL ? GSS_C_NO_CONTEXT : (gss_ctx_id_t)*context;
  struct gss_channel_bindings_struct native_bindings =
      cpkt_gss_native_bindings(bindings);
  gss_buffer_desc native_input = cpkt_gss_native_buffer(input), native_output;
  gss_name_t source = GSS_C_NO_NAME;
  gss_OID mechanism = GSS_C_NO_OID;
  gss_cred_id_t delegated = GSS_C_NO_CREDENTIAL;
  OM_uint32 status;
  if (context == NULL)
    return cpkt_gss_missing_handle_output(minor_out);
  if (!cpkt_gss_bindings_fit(bindings))
    return cpkt_gss_invalid_u32(minor_out);
  memset(&native_output, 0, sizeof(native_output));
  status = gss_accept_sec_context(
      &minor, &native_context, (gss_cred_id_t)credential,
      input == NULL ? GSS_C_NO_BUFFER : &native_input,
      bindings == NULL ? GSS_C_NO_CHANNEL_BINDINGS : &native_bindings,
      source_out == NULL ? NULL : &source,
      mechanism_out == NULL ? NULL : &mechanism, &native_output,
      &returned_flags, &returned_lifetime,
      delegated_out == NULL ? NULL : &delegated);
  if (context != NULL)
    *context = (cpkt_gss_context *)native_context;
  if (source_out != NULL)
    *source_out = (cpkt_gss_name *)source;
  if (mechanism_out != NULL)
    *mechanism_out = (const cpkt_gss_oid *)mechanism;
  cpkt_gss_publish_buffer(output, &native_output);
  if (returned_flags_out != NULL)
    *returned_flags_out = (cpkt_gss_flags)returned_flags;
  if (lifetime_out != NULL)
    *lifetime_out = (cpkt_gss_lifetime)returned_lifetime;
  if (delegated_out != NULL)
    *delegated_out = (cpkt_gss_credential *)delegated;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_context_lifetime. */
cpkt_gss_status cpkt_gss_context_lifetime(cpkt_gss_status *minor_out,
                                          const cpkt_gss_context *context,
                                          cpkt_gss_lifetime *lifetime_out) {
  OM_uint32 minor, lifetime = 0;
  OM_uint32 status = gss_context_time(&minor, (gss_ctx_id_t)context, &lifetime);
  if (lifetime_out != NULL)
    *lifetime_out = (cpkt_gss_lifetime)lifetime;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_get_mic. */
cpkt_gss_status cpkt_gss_get_mic(cpkt_gss_status *minor_out,
                                 const cpkt_gss_context *context,
                                 cpkt_gss_qop qop,
                                 const cpkt_gss_buffer *message,
                                 cpkt_gss_buffer *token_out) {
  OM_uint32 minor;
  gss_buffer_desc native_message = cpkt_gss_native_buffer(message), token;
  OM_uint32 status;
  if (!cpkt_gss_u32_fits(qop))
    return cpkt_gss_invalid_u32(minor_out);
  memset(&token, 0, sizeof(token));
  status = gss_get_mic(&minor, (gss_ctx_id_t)context, (gss_qop_t)qop,
                       &native_message, &token);
  cpkt_gss_publish_buffer(token_out, &token);
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_verify_mic. */
cpkt_gss_status cpkt_gss_verify_mic(cpkt_gss_status *minor_out,
                                    const cpkt_gss_context *context,
                                    const cpkt_gss_buffer *message,
                                    const cpkt_gss_buffer *token,
                                    cpkt_gss_qop *qop_out) {
  OM_uint32 minor;
  gss_qop_t qop = 0;
  gss_buffer_desc native_message = cpkt_gss_native_buffer(message),
                  native_token = cpkt_gss_native_buffer(token);
  OM_uint32 status = gss_verify_mic(&minor, (gss_ctx_id_t)context,
                                    &native_message, &native_token, &qop);
  if (qop_out != NULL)
    *qop_out = (cpkt_gss_qop)qop;
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation cpkt_gss_wrap.
 */
cpkt_gss_status cpkt_gss_wrap(cpkt_gss_status *minor_out,
                              const cpkt_gss_context *context,
                              int confidentiality_requested, cpkt_gss_qop qop,
                              const cpkt_gss_buffer *input,
                              int *confidentiality_out,
                              cpkt_gss_buffer *output_out) {
  OM_uint32 minor;
  int confidentiality = 0;
  gss_buffer_desc native_input = cpkt_gss_native_buffer(input), output;
  OM_uint32 status;
  if (!cpkt_gss_u32_fits(qop))
    return cpkt_gss_invalid_u32(minor_out);
  memset(&output, 0, sizeof(output));
  status = gss_wrap(&minor, (gss_ctx_id_t)context, confidentiality_requested,
                    (gss_qop_t)qop, &native_input, &confidentiality, &output);
  if (confidentiality_out != NULL)
    *confidentiality_out = confidentiality;
  cpkt_gss_publish_buffer(output_out, &output);
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_unwrap. */
cpkt_gss_status
cpkt_gss_unwrap(cpkt_gss_status *minor_out, const cpkt_gss_context *context,
                const cpkt_gss_buffer *input, cpkt_gss_buffer *output_out,
                int *confidentiality_out, cpkt_gss_qop *qop_out) {
  OM_uint32 minor;
  int confidentiality = 0;
  gss_qop_t qop = 0;
  gss_buffer_desc native_input = cpkt_gss_native_buffer(input), output;
  OM_uint32 status;
  memset(&output, 0, sizeof(output));
  status = gss_unwrap(&minor, (gss_ctx_id_t)context, &native_input, &output,
                      &confidentiality, &qop);
  if (confidentiality_out != NULL)
    *confidentiality_out = confidentiality;
  if (qop_out != NULL)
    *qop_out = (cpkt_gss_qop)qop;
  cpkt_gss_publish_buffer(output_out, &output);
  return cpkt_gss_finish(status, minor, minor_out);
}

/** Implements the documented public C89 GSSAPI facade operation
 * cpkt_gss_display_status. */
cpkt_gss_status cpkt_gss_display_status(cpkt_gss_status *minor_out,
                                        cpkt_gss_status status_value, int type,
                                        const cpkt_gss_oid *mechanism,
                                        cpkt_gss_status *message_context,
                                        cpkt_gss_buffer *text_out) {
  OM_uint32 minor, native_context = message_context == NULL
                                        ? 0
                                        : (OM_uint32)*message_context;
  gss_buffer_desc text;
  OM_uint32 status;
  if (!cpkt_gss_u32_fits(status_value) ||
      (message_context != NULL && !cpkt_gss_u32_fits(*message_context)))
    return cpkt_gss_invalid_u32(minor_out);
  memset(&text, 0, sizeof(text));
  status = gss_display_status(&minor, (OM_uint32)status_value, type,
                              (gss_OID)mechanism, &native_context, &text);
  if (message_context != NULL)
    *message_context = (cpkt_gss_status)native_context;
  cpkt_gss_publish_buffer(text_out, &text);
  return cpkt_gss_finish(status, minor, minor_out);
}
