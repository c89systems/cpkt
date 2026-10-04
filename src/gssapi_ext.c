#include <cpkt/gssapi.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <gssapi/gssapi_ext.h>
#include <gssapi/gssapi_krb5.h>

static cpkt_gss_status cpkt_gss_ext_result(OM_uint32 status, OM_uint32 minor,
                                           cpkt_gss_status *minor_out) {
  if (minor_out != NULL)
    *minor_out = (cpkt_gss_status)minor;
  return (cpkt_gss_status)status;
}

static cpkt_gss_status cpkt_gss_ext_invalid(cpkt_gss_status *minor_out) {
  return cpkt_gss_ext_result(GSS_S_CALL_BAD_STRUCTURE, EINVAL, minor_out);
}

/** C89 facade contract for cpkt_gss_release_buffer_set; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_release_buffer_set(cpkt_gss_status *minor_out,
                                            cpkt_gss_buffer_set **set) {
  OM_uint32 minor;
  gss_buffer_set_t native;
  OM_uint32 status;
  native = set == NULL ? GSS_C_NO_BUFFER_SET : (gss_buffer_set_t)*set;
  status = gss_release_buffer_set(&minor, &native);
  if (set != NULL)
    *set = (cpkt_gss_buffer_set *)native;
  return cpkt_gss_ext_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_create_buffer_set; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_create_buffer_set(cpkt_gss_status *minor_out,
                                           cpkt_gss_buffer_set **set_out) {
  OM_uint32 minor;
  gss_buffer_set_t native = GSS_C_NO_BUFFER_SET;
  OM_uint32 status;
  if (set_out == NULL)
    return cpkt_gss_ext_invalid(minor_out);
  *set_out = NULL;
  status = gss_create_empty_buffer_set(&minor, &native);
  *set_out = (cpkt_gss_buffer_set *)native;
  return cpkt_gss_ext_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_add_buffer_set_member; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_add_buffer_set_member(cpkt_gss_status *minor_out,
                                               const cpkt_gss_buffer *member,
                                               cpkt_gss_buffer_set **set) {
  OM_uint32 minor;
  gss_buffer_desc native_member;
  gss_buffer_set_t native_set;
  OM_uint32 status;
  if (member == NULL || set == NULL)
    return cpkt_gss_ext_invalid(minor_out);
  native_member.length = member->length;
  native_member.value = member->value;
  native_set = (gss_buffer_set_t)*set;
  status = gss_add_buffer_set_member(&minor, &native_member, &native_set);
  *set = (cpkt_gss_buffer_set *)native_set;
  return cpkt_gss_ext_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_buffer_set_count; see the public header for
 * ownership and callback lifetime. */
size_t cpkt_gss_buffer_set_count(const cpkt_gss_buffer_set *set) {
  return set == NULL ? 0 : ((const gss_buffer_set_desc *)set)->count;
}

/** C89 facade contract for cpkt_gss_buffer_set_at; see the public header for
 * ownership and callback lifetime. */
int cpkt_gss_buffer_set_at(const cpkt_gss_buffer_set *set, size_t index,
                           cpkt_gss_buffer *member_out) {
  const gss_buffer_set_desc *native = (const gss_buffer_set_desc *)set;
  if (member_out == NULL || native == NULL || index >= native->count)
    return 0;
  member_out->length = native->elements[index].length;
  member_out->value = native->elements[index].value;
  return 1;
}

static gss_iov_buffer_desc *cpkt_gss_iov_prepare(const cpkt_gss_iov *public_iov,
                                                 size_t count) {
  gss_iov_buffer_desc *native;
  size_t i;
  if (count == 0)
    return NULL;
  if (count > (size_t)INT_MAX || count > ((size_t)-1) / sizeof(*native))
    return NULL;
  native = (gss_iov_buffer_desc *)calloc(count, sizeof(*native));
  if (native == NULL)
    return NULL;
  for (i = 0; i < count; ++i) {
    native[i].type = (OM_uint32)public_iov[i].type;
    native[i].buffer.length = public_iov[i].buffer.length;
    native[i].buffer.value = public_iov[i].buffer.value;
  }
  return native;
}

static void cpkt_gss_iov_publish(cpkt_gss_iov *public_iov,
                                 const gss_iov_buffer_desc *native,
                                 size_t count) {
  size_t i;
  for (i = 0; i < count; ++i) {
    public_iov[i].type = native[i].type;
    public_iov[i].buffer.length = native[i].buffer.length;
    public_iov[i].buffer.value = native[i].buffer.value;
  }
}

static cpkt_gss_status cpkt_gss_iov_prepare_status(cpkt_gss_status *minor_out,
                                                   const cpkt_gss_iov *iov,
                                                   size_t count,
                                                   gss_iov_buffer_desc **out) {
  if ((iov == NULL && count != 0) || count > (size_t)INT_MAX)
    return cpkt_gss_ext_invalid(minor_out);
  {
    size_t index;
    for (index = 0; index < count; ++index) {
      if (iov[index].type > 0xffffffffUL)
        return cpkt_gss_ext_invalid(minor_out);
    }
  }
  *out = cpkt_gss_iov_prepare(iov, count);
  if (count != 0 && *out == NULL)
    return cpkt_gss_ext_result(GSS_S_FAILURE, ENOMEM, minor_out);
  return GSS_S_COMPLETE;
}

/* A single copy-in/copy-out path keeps native allocation flags and partial
 * outputs observable even when an IOV operation returns an error. */
static cpkt_gss_status cpkt_gss_iov_call(cpkt_gss_status *minor_out,
                                         const cpkt_gss_context *context,
                                         int conf, cpkt_gss_qop qop,
                                         int *conf_out, cpkt_gss_qop *qop_out,
                                         cpkt_gss_iov *iov, size_t count,
                                         int operation) {
  gss_iov_buffer_desc *native;
  OM_uint32 minor = 0;
  gss_qop_t native_qop = 0;
  int native_conf = 0;
  OM_uint32 status;
  if (qop > 0xffffffffUL)
    return cpkt_gss_ext_invalid(minor_out);
  status =
      (OM_uint32)cpkt_gss_iov_prepare_status(minor_out, iov, count, &native);
  if (status != GSS_S_COMPLETE)
    return status;
  switch (operation) {
  case 0:
    status = gss_wrap_iov(&minor, (gss_ctx_id_t)context, conf, (gss_qop_t)qop,
                          &native_conf, native, (int)count);
    break;
  case 1:
    status = gss_unwrap_iov(&minor, (gss_ctx_id_t)context, &native_conf,
                            &native_qop, native, (int)count);
    break;
  case 2:
    status =
        gss_wrap_iov_length(&minor, (gss_ctx_id_t)context, conf, (gss_qop_t)qop,
                            &native_conf, native, (int)count);
    break;
  case 3:
    status = gss_get_mic_iov(&minor, (gss_ctx_id_t)context, (gss_qop_t)qop,
                             native, (int)count);
    break;
  case 4:
    status = gss_get_mic_iov_length(&minor, (gss_ctx_id_t)context,
                                    (gss_qop_t)qop, native, (int)count);
    break;
  case 5:
    status = gss_verify_mic_iov(&minor, (gss_ctx_id_t)context, &native_qop,
                                native, (int)count);
    break;
  default:
    status = gss_release_iov_buffer(&minor, native, (int)count);
    break;
  }
  if (conf_out != NULL)
    *conf_out = native_conf;
  if (qop_out != NULL)
    *qop_out = native_qop;
  cpkt_gss_iov_publish(iov, native, count);
  free(native);
  return cpkt_gss_ext_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_wrap_iov; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_wrap_iov(cpkt_gss_status *minor_out,
                                  const cpkt_gss_context *context, int conf,
                                  cpkt_gss_qop qop, int *conf_out,
                                  cpkt_gss_iov *iov, size_t count) {
  return cpkt_gss_iov_call(minor_out, context, conf, qop, conf_out, NULL, iov,
                           count, 0);
}
/** C89 facade contract for cpkt_gss_unwrap_iov; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_unwrap_iov(cpkt_gss_status *minor_out,
                                    const cpkt_gss_context *context,
                                    int *conf_out, cpkt_gss_qop *qop_out,
                                    cpkt_gss_iov *iov, size_t count) {
  return cpkt_gss_iov_call(minor_out, context, 0, 0, conf_out, qop_out, iov,
                           count, 1);
}
/** C89 facade contract for cpkt_gss_wrap_iov_length; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_wrap_iov_length(cpkt_gss_status *minor_out,
                                         const cpkt_gss_context *context,
                                         int conf, cpkt_gss_qop qop,
                                         int *conf_out, cpkt_gss_iov *iov,
                                         size_t count) {
  return cpkt_gss_iov_call(minor_out, context, conf, qop, conf_out, NULL, iov,
                           count, 2);
}
/** C89 facade contract for cpkt_gss_get_mic_iov; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_get_mic_iov(cpkt_gss_status *minor_out,
                                     const cpkt_gss_context *context,
                                     cpkt_gss_qop qop, cpkt_gss_iov *iov,
                                     size_t count) {
  return cpkt_gss_iov_call(minor_out, context, 0, qop, NULL, NULL, iov, count,
                           3);
}
/** C89 facade contract for cpkt_gss_get_mic_iov_length; see the public header
 * for ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_get_mic_iov_length(cpkt_gss_status *minor_out,
                                            const cpkt_gss_context *context,
                                            cpkt_gss_qop qop, cpkt_gss_iov *iov,
                                            size_t count) {
  return cpkt_gss_iov_call(minor_out, context, 0, qop, NULL, NULL, iov, count,
                           4);
}
/** C89 facade contract for cpkt_gss_verify_mic_iov; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_verify_mic_iov(cpkt_gss_status *minor_out,
                                        const cpkt_gss_context *context,
                                        cpkt_gss_qop *qop_out,
                                        cpkt_gss_iov *iov, size_t count) {
  return cpkt_gss_iov_call(minor_out, context, 0, 0, NULL, qop_out, iov, count,
                           5);
}
/** C89 facade contract for cpkt_gss_release_iov; see the public header for
 * ownership and callback lifetime. */
cpkt_gss_status cpkt_gss_release_iov(cpkt_gss_status *minor_out,
                                     cpkt_gss_iov *iov, size_t count) {
  return cpkt_gss_iov_call(minor_out, NULL, 0, 0, NULL, NULL, iov, count, 6);
}

/** C89 facade contract for cpkt_gss_krb5_mechanism; see the public header for
 * ownership and callback lifetime. */
const cpkt_gss_oid *cpkt_gss_krb5_mechanism(void) {
  return (const cpkt_gss_oid *)gss_mech_krb5;
}
/** C89 facade contract for cpkt_gss_krb5_principal_name_type; see the public
 * header for ownership and callback lifetime. */
const cpkt_gss_oid *cpkt_gss_krb5_principal_name_type(void) {
  return (const cpkt_gss_oid *)GSS_KRB5_NT_PRINCIPAL_NAME;
}

static cpkt_gss_u64_words cpkt_gss_seq_words(uint64_t value) {
  cpkt_gss_u64_words words;
  words.high = (unsigned long)(value >> 32);
  words.low = (unsigned long)(uint32_t)value;
  return words;
}

static void cpkt_gss_lucid_key_copy(cpkt_gss_lucid_key *out,
                                    const gss_krb5_lucid_key_t *native) {
  out->type = native->type;
  out->length = native->length;
  out->data = native->data;
}

/** C89 facade contract for cpkt_gss_krb5_export_lucid_context; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status
cpkt_gss_krb5_export_lucid_context(cpkt_gss_status *minor_out,
                                   cpkt_gss_context **context,
                                   cpkt_gss_lucid_context **lucid_out) {
  cpkt_gss_lucid_context *view;
  gss_ctx_id_t native_context;
  gss_krb5_lucid_context_v1_t *native_lucid = NULL;
  void *native_lucid_value = NULL;
  OM_uint32 minor;
  OM_uint32 status;
  if (context == NULL || lucid_out == NULL)
    return cpkt_gss_ext_invalid(minor_out);
  *lucid_out = NULL;
  view = (cpkt_gss_lucid_context *)calloc(1, sizeof(*view));
  if (view == NULL)
    return cpkt_gss_ext_result(GSS_S_FAILURE, ENOMEM, minor_out);
  native_context = (gss_ctx_id_t)*context;
  status = gss_krb5_export_lucid_sec_context(&minor, &native_context, 1,
                                             &native_lucid_value);
  *context = (cpkt_gss_context *)native_context;
  if (status != GSS_S_COMPLETE) {
    free(view);
    return cpkt_gss_ext_result(status, minor, minor_out);
  }
  native_lucid = (gss_krb5_lucid_context_v1_t *)native_lucid_value;
  view->version = native_lucid->version;
  view->initiate = native_lucid->initiate;
  view->endtime = native_lucid->endtime;
  view->send_sequence = cpkt_gss_seq_words(native_lucid->send_seq);
  view->receive_sequence = cpkt_gss_seq_words(native_lucid->recv_seq);
  view->protocol = native_lucid->protocol;
  if (native_lucid->protocol == 0) {
    view->sign_algorithm = native_lucid->rfc1964_kd.sign_alg;
    view->seal_algorithm = native_lucid->rfc1964_kd.seal_alg;
    cpkt_gss_lucid_key_copy(&view->context_key,
                            &native_lucid->rfc1964_kd.ctx_key);
  } else {
    view->have_acceptor_subkey = native_lucid->cfx_kd.have_acceptor_subkey;
    cpkt_gss_lucid_key_copy(&view->context_key, &native_lucid->cfx_kd.ctx_key);
    cpkt_gss_lucid_key_copy(&view->acceptor_subkey,
                            &native_lucid->cfx_kd.acceptor_subkey);
  }
  view->internal = native_lucid;
  *lucid_out = view;
  return cpkt_gss_ext_result(status, minor, minor_out);
}

/** C89 facade contract for cpkt_gss_krb5_free_lucid_context; see the public
 * header for ownership and callback lifetime. */
cpkt_gss_status
cpkt_gss_krb5_free_lucid_context(cpkt_gss_status *minor_out,
                                 cpkt_gss_lucid_context **lucid) {
  OM_uint32 minor = 0;
  OM_uint32 status = GSS_S_COMPLETE;
  if (lucid != NULL && *lucid != NULL) {
    status = gss_krb5_free_lucid_sec_context(&minor, (*lucid)->internal);
    if (!GSS_ERROR(status)) {
      free(*lucid);
      *lucid = NULL;
    }
  }
  return cpkt_gss_ext_result(status, minor, minor_out);
}
