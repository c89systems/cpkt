#include <cpkt/gssapi.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <gssapi/gssapi_ext.h>
#include <gssapi/gssapi_krb5.h>

/** Applies the documented process-wide Kerberos trace callback. */
void cpkt_gss_set_trace_callback(cpkt_gss_trace_callback callback,
                                 void *context) {
  gss_krb5_set_trace_callback(callback, context);
}

struct cpkt_gss_krb_replay_cache {
  krb5_rcache native;
  krb5_auth_context cleanup_owner;
};

struct cpkt_gss_pac {
  gss_name_t name;
  gss_any_t native;
};

static gss_buffer_desc cpkt_gss_pac_mapping_type(void) {
  gss_buffer_desc type;
  type.length = sizeof("mspac") - 1U;
  type.value = (void *)"mspac";
  return type;
}

static cpkt_gss_status cpkt_gss_krb_result(OM_uint32 major, OM_uint32 minor,
                                           cpkt_gss_status *minor_out) {
  if (minor_out != NULL)
    *minor_out = minor;
  return major;
}

static cpkt_gss_status cpkt_gss_krb_bad_argument(cpkt_gss_status *minor_out) {
  return cpkt_gss_krb_result(GSS_S_CALL_BAD_STRUCTURE, EINVAL, minor_out);
}

/** C89 facade contract for cpkt_gss_krb_context_new; see the public header for
 * ownership and callback lifetime. */
int cpkt_gss_krb_context_new(cpkt_gss_krb_context **context_out) {
  krb5_context native = NULL;
  int status;
  if (context_out == NULL)
    return EINVAL;
  *context_out = NULL;
  status = krb5_init_context(&native);
  if (status == 0)
    *context_out = (cpkt_gss_krb_context *)native;
  return status;
}

/** C89 facade contract for cpkt_gss_krb_context_free; see the public header for
 * ownership and callback lifetime. */
void cpkt_gss_krb_context_free(cpkt_gss_krb_context **context) {
  if (context != NULL && *context != NULL) {
    krb5_free_context((krb5_context)*context);
    *context = NULL;
  }
}

/** C89 facade contract for cpkt_gss_krb_cache_resolve; see the public header
 * for ownership and callback lifetime. */
int cpkt_gss_krb_cache_resolve(cpkt_gss_krb_context *context, const char *name,
                               cpkt_gss_krb_cache **cache_out) {
  krb5_ccache native = NULL;
  int status;
  if (context == NULL || name == NULL || cache_out == NULL)
    return EINVAL;
  *cache_out = NULL;
  status = krb5_cc_resolve((krb5_context)context, name, &native);
  if (status == 0)
    *cache_out = (cpkt_gss_krb_cache *)native;
  return status;
}

/** C89 facade contract for cpkt_gss_krb_cache_close; see the public header for
 * ownership and callback lifetime. */
int cpkt_gss_krb_cache_close(cpkt_gss_krb_context *context,
                             cpkt_gss_krb_cache **cache) {
  int status;
  if (context == NULL || cache == NULL)
    return EINVAL;
  if (*cache == NULL)
    return 0;
  status = krb5_cc_close((krb5_context)context, (krb5_ccache)*cache);
  *cache = NULL;
  return status;
}

/** C89 facade contract for cpkt_gss_krb_keytab_resolve; see the public header
 * for ownership and callback lifetime. */
int cpkt_gss_krb_keytab_resolve(cpkt_gss_krb_context *context, const char *name,
                                cpkt_gss_krb_keytab **keytab_out) {
  krb5_keytab native = NULL;
  int status;
  if (context == NULL || name == NULL || keytab_out == NULL)
    return EINVAL;
  *keytab_out = NULL;
  status = krb5_kt_resolve((krb5_context)context, name, &native);
  if (status == 0)
    *keytab_out = (cpkt_gss_krb_keytab *)native;
  return status;
}

/** C89 facade contract for cpkt_gss_krb_keytab_close; see the public header for
 * ownership and callback lifetime. */
int cpkt_gss_krb_keytab_close(cpkt_gss_krb_context *context,
                              cpkt_gss_krb_keytab **keytab) {
  int status;
  if (context == NULL || keytab == NULL)
    return EINVAL;
  if (*keytab == NULL)
    return 0;
  status = krb5_kt_close((krb5_context)context, (krb5_keytab)*keytab);
  *keytab = NULL;
  return status;
}

/** C89 facade contract for cpkt_gss_krb_principal_parse; see the public header
 * for ownership and callback lifetime. */
int cpkt_gss_krb_principal_parse(cpkt_gss_krb_context *context,
                                 const char *name,
                                 cpkt_gss_krb_principal **principal_out) {
  krb5_principal native = NULL;
  int status;
  if (context == NULL || name == NULL || principal_out == NULL)
    return EINVAL;
  *principal_out = NULL;
  status = krb5_parse_name((krb5_context)context, name, &native);
  if (status == 0)
    *principal_out = (cpkt_gss_krb_principal *)native;
  return status;
}

/** C89 facade contract for cpkt_gss_krb_principal_free; see the public header
 * for ownership and callback lifetime. */
void cpkt_gss_krb_principal_free(cpkt_gss_krb_context *context,
                                 cpkt_gss_krb_principal **principal) {
  if (context != NULL && principal != NULL && *principal != NULL) {
    krb5_free_principal((krb5_context)context, (krb5_principal)*principal);
    *principal = NULL;
  }
}

/** C89 facade contract for cpkt_gss_krb_replay_cache_new; see the public header
 * for ownership and callback lifetime. */
int cpkt_gss_krb_replay_cache_new(
    cpkt_gss_krb_context *context,
    cpkt_gss_krb_replay_cache **replay_cache_out) {
  cpkt_gss_krb_replay_cache *cache;
  int status;
  if (context == NULL || replay_cache_out == NULL)
    return EINVAL;
  *replay_cache_out = NULL;
  cache = (cpkt_gss_krb_replay_cache *)calloc(1, sizeof(*cache));
  if (cache == NULL)
    return ENOMEM;
  status = krb5_auth_con_init((krb5_context)context, &cache->cleanup_owner);
  if (status != 0) {
    free(cache);
    return status;
  }
  status = krb5_get_server_rcache((krb5_context)context, NULL, &cache->native);
  if (status != 0) {
    krb5_auth_con_free((krb5_context)context, cache->cleanup_owner);
    free(cache);
    return status;
  }
  status = krb5_auth_con_setrcache((krb5_context)context, cache->cleanup_owner,
                                   cache->native);
  if (status != 0) {
    /* This MIT Kerberos operation cannot fail after auth-construction. */
    krb5_auth_con_free((krb5_context)context, cache->cleanup_owner);
    free(cache);
    return status;
  }
  *replay_cache_out = cache;
  return 0;
}

/** C89 facade contract for cpkt_gss_krb_replay_cache_free; see the public
 * header for ownership and callback lifetime. */
void cpkt_gss_krb_replay_cache_free(cpkt_gss_krb_context *context,
                                    cpkt_gss_krb_replay_cache **replay_cache) {
  if (context != NULL && replay_cache != NULL && *replay_cache != NULL) {
    krb5_auth_con_free((krb5_context)context, (*replay_cache)->cleanup_owner);
    free(*replay_cache);
    *replay_cache = NULL;
  }
}

/** C89 facade contract for cpkt_gss_krb5_set_credential_replay_cache; see the
 * public header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_krb5_set_credential_replay_cache(
    cpkt_gss_status *minor_out, cpkt_gss_krb_context *context,
    cpkt_gss_credential *credential, cpkt_gss_krb_replay_cache **replay_cache) {
  OM_uint32 minor, status;
  if (context == NULL || credential == NULL || replay_cache == NULL ||
      *replay_cache == NULL)
    return cpkt_gss_krb_bad_argument(minor_out);
  status = gss_krb5_set_cred_rcache(&minor, (gss_cred_id_t)credential,
                                    (*replay_cache)->native);
  if (status == GSS_S_COMPLETE) {
    (void)krb5_auth_con_setrcache((krb5_context)context,
                                  (*replay_cache)->cleanup_owner, NULL);
    cpkt_gss_krb_replay_cache_free(context, replay_cache);
  }
  return cpkt_gss_krb_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_krb5_import_credential; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_krb5_import_credential(
    cpkt_gss_status *minor_out, cpkt_gss_krb_cache *cache,
    cpkt_gss_krb_principal *principal, cpkt_gss_krb_keytab *keytab,
    cpkt_gss_credential **credential_out) {
  gss_cred_id_t native = GSS_C_NO_CREDENTIAL;
  OM_uint32 minor;
  OM_uint32 status;
  if (credential_out == NULL)
    return cpkt_gss_krb_bad_argument(minor_out);
  *credential_out = NULL;
  status = gss_krb5_import_cred(&minor, (krb5_ccache)cache,
                                (krb5_principal)principal, (krb5_keytab)keytab,
                                &native);
  *credential_out = (cpkt_gss_credential *)native;
  return cpkt_gss_krb_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_krb5_copy_cache; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_krb5_copy_cache(cpkt_gss_status *minor_out,
                                         const cpkt_gss_credential *credential,
                                         cpkt_gss_krb_cache *cache) {
  OM_uint32 minor;
  OM_uint32 status = gss_krb5_copy_ccache(&minor, (gss_cred_id_t)credential,
                                          (krb5_ccache)cache);
  return cpkt_gss_krb_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_krb5_set_enctypes; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_krb5_set_enctypes(cpkt_gss_status *minor_out,
                                           cpkt_gss_credential *credential,
                                           const long *enctypes, size_t count) {
  krb5_enctype *native;
  size_t index;
  OM_uint32 minor;
  OM_uint32 status;
  if (credential == NULL || (enctypes == NULL && count != 0) ||
      count > 0xffffffffUL || count > ((size_t)-1) / sizeof(*native))
    return cpkt_gss_krb_bad_argument(minor_out);
  native = count == 0 ? NULL : (krb5_enctype *)malloc(count * sizeof(*native));
  if (count != 0 && native == NULL)
    return cpkt_gss_krb_result(GSS_S_FAILURE, ENOMEM, minor_out);
  for (index = 0; index < count; ++index) {
    if (enctypes[index] < INT_MIN || enctypes[index] > INT_MAX) {
      free(native);
      return cpkt_gss_krb_bad_argument(minor_out);
    }
    native[index] = (krb5_enctype)enctypes[index];
  }
  status = gss_krb5_set_allowable_enctypes(&minor, (gss_cred_id_t)credential,
                                           (OM_uint32)count, native);
  free(native);
  return cpkt_gss_krb_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_krb5_get_ticket_flags; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_krb5_get_ticket_flags(cpkt_gss_status *minor_out,
                                               const cpkt_gss_context *context,
                                               unsigned long *flags_out) {
  krb5_flags native = 0;
  OM_uint32 minor;
  OM_uint32 status =
      gss_krb5_get_tkt_flags(&minor, (gss_ctx_id_t)context, &native);
  if (flags_out != NULL)
    *flags_out = (unsigned long)(OM_uint32)native;
  return cpkt_gss_krb_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_krb5_extract_authentication_time; see the
 * public header for ownership and callback lifetime. */
cpkt_gss_status
cpkt_gss_krb5_extract_authentication_time(cpkt_gss_status *minor_out,
                                          const cpkt_gss_context *context,
                                          long *timestamp_out) {
  krb5_timestamp native = 0;
  OM_uint32 minor;
  OM_uint32 status = gsskrb5_extract_authtime_from_sec_context(
      &minor, (gss_ctx_id_t)context, &native);
  if (timestamp_out != NULL)
    *timestamp_out = (long)native;
  return cpkt_gss_krb_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_krb5_extract_authorization_data; see the
 * public header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_krb5_extract_authorization_data(
    cpkt_gss_status *minor_out, const cpkt_gss_context *context, int data_type,
    cpkt_gss_buffer *data_out) {
  gss_buffer_desc native = {0, NULL};
  OM_uint32 minor, ignored;
  OM_uint32 status = gsskrb5_extract_authz_data_from_sec_context(
      &minor, (gss_ctx_id_t)context, data_type, &native);
  if (data_out == NULL) {
    if (native.value != NULL)
      (void)gss_release_buffer(&ignored, &native);
  } else {
    data_out->length = native.length;
    data_out->value = native.value;
  }
  return cpkt_gss_krb_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_krb5_cache_name; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_krb5_cache_name(cpkt_gss_status *minor_out,
                                         const char *name,
                                         const char **previous_name_out) {
  OM_uint32 minor;
  OM_uint32 status = gss_krb5_ccache_name(&minor, name, previous_name_out);
  return cpkt_gss_krb_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_krb5_register_acceptor_identity; see the
 * public header for ownership and callback lifetime. */
cpkt_gss_status
cpkt_gss_krb5_register_acceptor_identity(const char *keytab_name) {
  return krb5_gss_register_acceptor_identity(keytab_name);
}

/** C89 facade contract for cpkt_gss_map_name_to_pac; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_map_name_to_pac(cpkt_gss_status *minor_out,
                                         cpkt_gss_name *name,
                                         int authenticated_only,
                                         cpkt_gss_pac **pac_out) {
  cpkt_gss_pac *pac;
  gss_buffer_desc type = cpkt_gss_pac_mapping_type();
  OM_uint32 minor, status;
  if (name == NULL || pac_out == NULL)
    return cpkt_gss_krb_bad_argument(minor_out);
  *pac_out = NULL;
  pac = (cpkt_gss_pac *)calloc(1, sizeof(*pac));
  if (pac == NULL)
    return cpkt_gss_krb_result(GSS_S_FAILURE, ENOMEM, minor_out);
  status = gss_duplicate_name(&minor, (gss_name_t)name, &pac->name);
  if (status != GSS_S_COMPLETE) {
    free(pac);
    return cpkt_gss_krb_result(status, minor, minor_out);
  }
  status = gss_map_name_to_any(&minor, pac->name, authenticated_only, &type,
                               &pac->native);
  if (status != GSS_S_COMPLETE) {
    OM_uint32 ignored;
    (void)gss_release_name(&ignored, &pac->name);
    free(pac);
    return cpkt_gss_krb_result(status, minor, minor_out);
  }
  *pac_out = pac;
  return cpkt_gss_krb_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_pac_release; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_pac_release(cpkt_gss_status *minor_out,
                                     cpkt_gss_pac **pac) {
  gss_buffer_desc type = cpkt_gss_pac_mapping_type();
  OM_uint32 minor = 0, status = GSS_S_COMPLETE;
  if (pac != NULL && *pac != NULL) {
    status = gss_release_any_name_mapping(&minor, (*pac)->name, &type,
                                          &(*pac)->native);
    if (status == GSS_S_COMPLETE) {
      OM_uint32 ignored;
      (void)gss_release_name(&ignored, &(*pac)->name);
      free(*pac);
      *pac = NULL;
    }
  }
  return cpkt_gss_krb_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_pac_types; see the public header for
 * ownership and callback lifetime. */
int cpkt_gss_pac_types(cpkt_gss_krb_context *context, const cpkt_gss_pac *pac,
                       unsigned long **types_out, size_t *count_out) {
  krb5_ui_4 *native_types = NULL;
  unsigned long *types;
  size_t count = 0, i;
  int status;
  if (types_out == NULL || count_out == NULL || context == NULL || pac == NULL)
    return EINVAL;
  *types_out = NULL;
  *count_out = 0;
  status = krb5_pac_get_types((krb5_context)context, (krb5_pac)pac->native,
                              &count, &native_types);
  if (status != 0)
    return status;
  if (count > ((size_t)-1) / sizeof(*types)) {
    free(native_types);
    return ENOMEM;
  }
  types = count == 0 ? NULL : (unsigned long *)malloc(count * sizeof(*types));
  if (count != 0 && types == NULL) {
    free(native_types);
    return ENOMEM;
  }
  for (i = 0; i < count; ++i)
    types[i] = native_types[i];
  free(native_types);
  *types_out = types;
  *count_out = count;
  return 0;
}

/** C89 facade contract for cpkt_gss_pac_types_free; see the public header for
 * ownership and callback lifetime. */
void cpkt_gss_pac_types_free(unsigned long **types) {
  if (types != NULL) {
    free(*types);
    *types = NULL;
  }
}

/** C89 facade contract for cpkt_gss_pac_get_buffer; see the public header for
 * ownership and callback lifetime. */
int cpkt_gss_pac_get_buffer(cpkt_gss_krb_context *context,
                            const cpkt_gss_pac *pac, unsigned long type,
                            cpkt_gss_buffer *buffer_out) {
  krb5_data native;
  void *copy;
  int status;
  if (context == NULL || pac == NULL || buffer_out == NULL ||
      type > 0xffffffffUL)
    return EINVAL;
  buffer_out->length = 0;
  buffer_out->value = NULL;
  memset(&native, 0, sizeof(native));
  status = krb5_pac_get_buffer((krb5_context)context, (krb5_pac)pac->native,
                               (krb5_ui_4)type, &native);
  if (status != 0)
    return status;
  copy = native.length == 0 ? NULL : malloc(native.length);
  if (native.length != 0 && copy == NULL) {
    krb5_free_data_contents((krb5_context)context, &native);
    return ENOMEM;
  }
  if (native.length != 0)
    memcpy(copy, native.data, native.length);
  buffer_out->length = native.length;
  buffer_out->value = copy;
  krb5_free_data_contents((krb5_context)context, &native);
  return 0;
}

/** C89 facade contract for cpkt_gss_pac_buffer_free; see the public header for
 * ownership and callback lifetime. */
void cpkt_gss_pac_buffer_free(cpkt_gss_buffer *buffer) {
  if (buffer != NULL) {
    free(buffer->value);
    buffer->value = NULL;
    buffer->length = 0;
  }
}
