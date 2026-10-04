#include <cpkt/gssapi.h>

#include <errno.h>
#include <stdlib.h>

#include <gssapi/gssapi_ext.h>

static cpkt_gss_status cpkt_gss_cred_result(OM_uint32 major, OM_uint32 minor,
                                            cpkt_gss_status *minor_out) {
  if (minor_out != NULL)
    *minor_out = minor;
  return major;
}

static cpkt_gss_status cpkt_gss_cred_bad_argument(cpkt_gss_status *minor_out) {
  return cpkt_gss_cred_result(GSS_S_CALL_BAD_STRUCTURE, EINVAL, minor_out);
}

static gss_buffer_desc cpkt_gss_cred_buffer(const cpkt_gss_buffer *value) {
  gss_buffer_desc native;
  native.length = value == NULL ? 0 : value->length;
  native.value = value == NULL ? NULL : value->value;
  return native;
}

static int cpkt_gss_cred_store_build(const cpkt_gss_store *public_store,
                                     gss_key_value_set_desc *native_store) {
  size_t i;
  native_store->count = 0;
  native_store->elements = NULL;
  if (public_store == NULL)
    return 0;
  if (public_store->count > 0xffffffffUL ||
      public_store->count > ((size_t)-1) / sizeof(*native_store->elements) ||
      (public_store->count != 0 && public_store->entries == NULL))
    return EINVAL;
  if (public_store->count != 0) {
    native_store->elements = (gss_key_value_element_desc *)calloc(
        public_store->count, sizeof(*native_store->elements));
    if (native_store->elements == NULL)
      return ENOMEM;
  }
  native_store->count = (OM_uint32)public_store->count;
  for (i = 0; i < public_store->count; ++i) {
    native_store->elements[i].key = public_store->entries[i].key;
    native_store->elements[i].value = public_store->entries[i].value;
  }
  return 0;
}

static void cpkt_gss_cred_publish(gss_cred_id_t native_credential,
                                  gss_OID_set native_mechanisms,
                                  OM_uint32 native_lifetime,
                                  cpkt_gss_credential **credential_out,
                                  cpkt_gss_oid_set **mechanisms_out,
                                  cpkt_gss_lifetime *lifetime_out) {
  OM_uint32 ignored;
  if (credential_out != NULL)
    *credential_out = (cpkt_gss_credential *)native_credential;
  else if (native_credential != GSS_C_NO_CREDENTIAL)
    (void)gss_release_cred(&ignored, &native_credential);
  if (mechanisms_out != NULL)
    *mechanisms_out = (cpkt_gss_oid_set *)native_mechanisms;
  else if (native_mechanisms != GSS_C_NO_OID_SET)
    (void)gss_release_oid_set(&ignored, &native_mechanisms);
  if (lifetime_out != NULL)
    *lifetime_out = native_lifetime;
}

static cpkt_gss_status cpkt_gss_cred_acquire(
    cpkt_gss_status *minor_out, const cpkt_gss_name *name,
    const cpkt_gss_buffer *password, const cpkt_gss_credential *impersonator,
    cpkt_gss_lifetime lifetime, const cpkt_gss_oid_set *mechanisms, int usage,
    const cpkt_gss_store *store, cpkt_gss_credential **credential_out,
    cpkt_gss_oid_set **actual_mechanisms_out,
    cpkt_gss_lifetime *actual_lifetime_out, int operation) {
  gss_key_value_set_desc native_store;
  gss_buffer_desc native_password = cpkt_gss_cred_buffer(password);
  gss_cred_id_t native_credential = GSS_C_NO_CREDENTIAL;
  gss_OID_set native_mechanisms = GSS_C_NO_OID_SET;
  OM_uint32 native_lifetime = 0, minor, status;
  if (credential_out == NULL || lifetime > 0xffffffffUL)
    return cpkt_gss_cred_bad_argument(minor_out);
  *credential_out = NULL;
  if (actual_mechanisms_out != NULL)
    *actual_mechanisms_out = NULL;
  if (actual_lifetime_out != NULL)
    *actual_lifetime_out = 0;
  {
    int store_error = cpkt_gss_cred_store_build(store, &native_store);
    if (store_error != 0)
      return cpkt_gss_cred_result(GSS_S_FAILURE, store_error, minor_out);
  }
  if (operation == 0)
    status = gss_acquire_cred_from(
        &minor, (gss_name_t)name, (OM_uint32)lifetime, (gss_OID_set)mechanisms,
        usage, store == NULL ? NULL : &native_store, &native_credential,
        &native_mechanisms, &native_lifetime);
  else if (operation == 1)
    status = gss_acquire_cred_with_password(
        &minor, (gss_name_t)name, &native_password, (OM_uint32)lifetime,
        (gss_OID_set)mechanisms, usage, &native_credential, &native_mechanisms,
        &native_lifetime);
  else
    status = gss_acquire_cred_impersonate_name(
        &minor, (gss_cred_id_t)impersonator, (gss_name_t)name,
        (OM_uint32)lifetime, (gss_OID_set)mechanisms, usage, &native_credential,
        &native_mechanisms, &native_lifetime);
  free(native_store.elements);
  cpkt_gss_cred_publish(native_credential, native_mechanisms, native_lifetime,
                        credential_out, actual_mechanisms_out,
                        actual_lifetime_out);
  return cpkt_gss_cred_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_acquire_credential_from; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_acquire_credential_from(
    cpkt_gss_status *minor_out, const cpkt_gss_name *name,
    cpkt_gss_lifetime lifetime, const cpkt_gss_oid_set *mechanisms, int usage,
    const cpkt_gss_store *store, cpkt_gss_credential **credential_out,
    cpkt_gss_oid_set **actual_mechanisms_out,
    cpkt_gss_lifetime *actual_lifetime_out) {
  return cpkt_gss_cred_acquire(minor_out, name, NULL, NULL, lifetime,
                               mechanisms, usage, store, credential_out,
                               actual_mechanisms_out, actual_lifetime_out, 0);
}

/** C89 facade contract for cpkt_gss_acquire_credential_with_password; see the
 * public header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_acquire_credential_with_password(
    cpkt_gss_status *minor_out, const cpkt_gss_name *name,
    const cpkt_gss_buffer *password, cpkt_gss_lifetime lifetime,
    const cpkt_gss_oid_set *mechanisms, int usage,
    cpkt_gss_credential **credential_out,
    cpkt_gss_oid_set **actual_mechanisms_out,
    cpkt_gss_lifetime *actual_lifetime_out) {
  if (password == NULL)
    return cpkt_gss_cred_bad_argument(minor_out);
  return cpkt_gss_cred_acquire(minor_out, name, password, NULL, lifetime,
                               mechanisms, usage, NULL, credential_out,
                               actual_mechanisms_out, actual_lifetime_out, 1);
}

/** C89 facade contract for cpkt_gss_acquire_credential_impersonating; see the
 * public header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_acquire_credential_impersonating(
    cpkt_gss_status *minor_out, const cpkt_gss_credential *impersonator,
    const cpkt_gss_name *name, cpkt_gss_lifetime lifetime,
    const cpkt_gss_oid_set *mechanisms, int usage,
    cpkt_gss_credential **credential_out,
    cpkt_gss_oid_set **actual_mechanisms_out,
    cpkt_gss_lifetime *actual_lifetime_out) {
  if (impersonator == NULL)
    return cpkt_gss_cred_bad_argument(minor_out);
  return cpkt_gss_cred_acquire(minor_out, name, NULL, impersonator, lifetime,
                               mechanisms, usage, NULL, credential_out,
                               actual_mechanisms_out, actual_lifetime_out, 2);
}

/** C89 facade contract for cpkt_gss_add_credential; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_add_credential(
    cpkt_gss_status *minor_out, const cpkt_gss_credential *input,
    const cpkt_gss_name *name, const cpkt_gss_oid *mechanism, int usage,
    cpkt_gss_lifetime initiator_lifetime, cpkt_gss_lifetime acceptor_lifetime,
    const cpkt_gss_store *store, const cpkt_gss_buffer *password,
    const cpkt_gss_credential *impersonator,
    cpkt_gss_credential **credential_out,
    cpkt_gss_oid_set **actual_mechanisms_out,
    cpkt_gss_lifetime *initiator_lifetime_out,
    cpkt_gss_lifetime *acceptor_lifetime_out) {
  gss_key_value_set_desc native_store;
  gss_buffer_desc native_password = cpkt_gss_cred_buffer(password);
  gss_cred_id_t native_credential = GSS_C_NO_CREDENTIAL;
  gss_OID_set native_mechanisms = GSS_C_NO_OID_SET;
  OM_uint32 initiator_actual = 0, acceptor_actual = 0, minor, status;
  if (credential_out == NULL || initiator_lifetime > 0xffffffffUL ||
      acceptor_lifetime > 0xffffffffUL ||
      (store != NULL && (password != NULL || impersonator != NULL)) ||
      (password != NULL && impersonator != NULL))
    return cpkt_gss_cred_bad_argument(minor_out);
  *credential_out = NULL;
  if (actual_mechanisms_out != NULL)
    *actual_mechanisms_out = NULL;
  if (initiator_lifetime_out != NULL)
    *initiator_lifetime_out = 0;
  if (acceptor_lifetime_out != NULL)
    *acceptor_lifetime_out = 0;
  {
    int store_error = cpkt_gss_cred_store_build(store, &native_store);
    if (store_error != 0)
      return cpkt_gss_cred_result(GSS_S_FAILURE, store_error, minor_out);
  }
  if (store != NULL)
    status = gss_add_cred_from(
        &minor, (gss_cred_id_t)input, (gss_name_t)name, (gss_OID)mechanism,
        usage, (OM_uint32)initiator_lifetime, (OM_uint32)acceptor_lifetime,
        &native_store, &native_credential, &native_mechanisms,
        &initiator_actual, &acceptor_actual);
  else if (password != NULL)
    status = gss_add_cred_with_password(
        &minor, (gss_cred_id_t)input, (gss_name_t)name, (gss_OID)mechanism,
        &native_password, usage, (OM_uint32)initiator_lifetime,
        (OM_uint32)acceptor_lifetime, &native_credential, &native_mechanisms,
        &initiator_actual, &acceptor_actual);
  else if (impersonator != NULL)
    status = gss_add_cred_impersonate_name(
        &minor, (gss_cred_id_t)input, (gss_cred_id_t)impersonator,
        (gss_name_t)name, (gss_OID)mechanism, usage,
        (OM_uint32)initiator_lifetime, (OM_uint32)acceptor_lifetime,
        &native_credential, &native_mechanisms, &initiator_actual,
        &acceptor_actual);
  else
    status =
        gss_add_cred(&minor, (gss_cred_id_t)input, (gss_name_t)name,
                     (gss_OID)mechanism, usage, (OM_uint32)initiator_lifetime,
                     (OM_uint32)acceptor_lifetime, &native_credential,
                     &native_mechanisms, &initiator_actual, &acceptor_actual);
  free(native_store.elements);
  cpkt_gss_cred_publish(native_credential, native_mechanisms, initiator_actual,
                        credential_out, actual_mechanisms_out,
                        initiator_lifetime_out);
  if (acceptor_lifetime_out != NULL)
    *acceptor_lifetime_out = acceptor_actual;
  return cpkt_gss_cred_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_inquire_credential_by_mechanism; see the
 * public header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_inquire_credential_by_mechanism(
    cpkt_gss_status *minor_out, const cpkt_gss_credential *credential,
    const cpkt_gss_oid *mechanism, cpkt_gss_name **name_out,
    cpkt_gss_lifetime *initiator_lifetime_out,
    cpkt_gss_lifetime *acceptor_lifetime_out, int *usage_out) {
  gss_name_t native_name = GSS_C_NO_NAME;
  OM_uint32 initiator = 0, acceptor = 0, minor, ignored;
  gss_cred_usage_t usage = 0;
  OM_uint32 status = gss_inquire_cred_by_mech(&minor, (gss_cred_id_t)credential,
                                              (gss_OID)mechanism, &native_name,
                                              &initiator, &acceptor, &usage);
  if (name_out != NULL)
    *name_out = (cpkt_gss_name *)native_name;
  else if (native_name != GSS_C_NO_NAME)
    (void)gss_release_name(&ignored, &native_name);
  if (initiator_lifetime_out != NULL)
    *initiator_lifetime_out = initiator;
  if (acceptor_lifetime_out != NULL)
    *acceptor_lifetime_out = acceptor;
  if (usage_out != NULL)
    *usage_out = usage;
  return cpkt_gss_cred_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_inquire_credential_by_oid; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_inquire_credential_by_oid(
    cpkt_gss_status *minor_out, const cpkt_gss_credential *credential,
    const cpkt_gss_oid *desired_object, cpkt_gss_buffer_set **data_out) {
  gss_buffer_set_t native = GSS_C_NO_BUFFER_SET;
  OM_uint32 minor;
  OM_uint32 status;
  if (data_out == NULL)
    return cpkt_gss_cred_bad_argument(minor_out);
  *data_out = NULL;
  status = gss_inquire_cred_by_oid(&minor, (gss_cred_id_t)credential,
                                   (gss_OID)desired_object, &native);
  *data_out = (cpkt_gss_buffer_set *)native;
  return cpkt_gss_cred_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_set_credential_option; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_set_credential_option(cpkt_gss_status *minor_out,
                                               cpkt_gss_credential **credential,
                                               const cpkt_gss_oid *option,
                                               const cpkt_gss_buffer *value) {
  gss_cred_id_t native;
  gss_buffer_desc native_value = cpkt_gss_cred_buffer(value);
  OM_uint32 minor, status;
  if (credential == NULL)
    return cpkt_gss_cred_bad_argument(minor_out);
  native = (gss_cred_id_t)*credential;
  status = gss_set_cred_option(&minor, &native, (gss_OID)option, &native_value);
  *credential = (cpkt_gss_credential *)native;
  return cpkt_gss_cred_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_set_negotiation_mechanisms; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status
cpkt_gss_set_negotiation_mechanisms(cpkt_gss_status *minor_out,
                                    cpkt_gss_credential *credential,
                                    const cpkt_gss_oid_set *mechanisms) {
  OM_uint32 minor;
  OM_uint32 status = gss_set_neg_mechs(&minor, (gss_cred_id_t)credential,
                                       (gss_OID_set)mechanisms);
  return cpkt_gss_cred_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_store_credential; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_store_credential(
    cpkt_gss_status *minor_out, const cpkt_gss_credential *credential,
    int usage, const cpkt_gss_oid *mechanism, int overwrite,
    int default_credential, const cpkt_gss_store *store,
    cpkt_gss_oid_set **stored_mechanisms_out, int *stored_usage_out) {
  gss_key_value_set_desc native_store;
  gss_OID_set native_mechanisms = GSS_C_NO_OID_SET;
  gss_cred_usage_t native_usage = 0;
  OM_uint32 minor, status, ignored;
  {
    int store_error = cpkt_gss_cred_store_build(store, &native_store);
    if (store_error != 0)
      return cpkt_gss_cred_result(GSS_S_FAILURE, store_error, minor_out);
  }
  if (store == NULL)
    status = gss_store_cred(&minor, (gss_cred_id_t)credential, usage,
                            (gss_OID)mechanism, overwrite, default_credential,
                            &native_mechanisms, &native_usage);
  else
    status = gss_store_cred_into(
        &minor, (gss_cred_id_t)credential, usage, (gss_OID)mechanism, overwrite,
        default_credential, &native_store, &native_mechanisms, &native_usage);
  free(native_store.elements);
  if (stored_mechanisms_out != NULL)
    *stored_mechanisms_out = (cpkt_gss_oid_set *)native_mechanisms;
  else if (native_mechanisms != GSS_C_NO_OID_SET)
    (void)gss_release_oid_set(&ignored, &native_mechanisms);
  if (stored_usage_out != NULL)
    *stored_usage_out = native_usage;
  return cpkt_gss_cred_result(status, minor, minor_out);
}
