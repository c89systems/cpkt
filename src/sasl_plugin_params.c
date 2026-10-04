#include <cpkt/sasl_plugin.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

#include <sasl/sasl.h>
#include <sasl/saslplug.h>

#include "sasl_private.h"

/** C89 facade contract for cpkt_sasl_plugin_client_params_from_native; see the
 * public header for ownership and callback lifetime. */
void cpkt_sasl_plugin_client_params_from_native(
    cpkt_sasl_plugin_client_view *view, sasl_client_params_t *native) {
  cpkt_sasl_client_params *out = &view->params;
  memset(view, 0, sizeof(*view));
  out->service = native->service;
  out->server_name = native->serverFQDN;
  out->client_name = native->clientFQDN;
  out->local_endpoint = native->iplocalport;
  out->remote_endpoint = native->ipremoteport;
  if (native->prompt_supp != NULL &&
      native->prompt_supp[0].id != SASL_CB_LIST_END)
    out->prompt_callbacks =
        cpkt_sasl_callbacks_from_native_context(native->prompt_supp[0].context);
  out->service_length = native->servicelen;
  out->server_length = native->slen;
  out->client_length = native->clen;
  out->local_length = native->iploclen;
  out->remote_length = native->ipremlen;
  out->security.minimum_ssf = native->props.min_ssf;
  out->security.maximum_ssf = native->props.max_ssf;
  out->security.maximum_buffer_bytes = native->props.maxbufsize;
  out->security.security_flags = native->props.security_flags;
  out->security.property_names = native->props.property_names;
  out->security.property_values = native->props.property_values;
  out->external_ssf = native->external_ssf;
  out->gss_credentials = (const cpkt_gss_credential *)native->gss_creds;
  if (native->cbinding != NULL) {
    view->binding.name = native->cbinding->name;
    view->binding.critical = native->cbinding->critical;
    view->binding.byte_count = native->cbinding->len;
    view->binding.data = native->cbinding->data;
    out->channel_binding = &view->binding;
  }
  if (native->http_request != NULL) {
    view->request.method = native->http_request->method;
    view->request.uri = native->http_request->uri;
    view->request.entity = native->http_request->entity;
    view->request.entity_byte_count = native->http_request->elen;
    view->request.non_persistent = native->http_request->non_persist;
    out->http_request = &view->request;
  }
  out->channel_binding_disposition = native->cbindingdisp;
  out->flags = native->flags;
  out->parameter_version = native->param_version;
  out->utils = cpkt_sasl_plugin_utils_for_native(native->utils);
  out->canonicalize =
      native->canon_user == NULL ? NULL : cpkt_sasl_plugin_client_canonicalize;
  out->internal = native;
}

static int cpkt_sasl_plugin_native_encode(cpkt_sasl_plugin_output *self,
                                          const cpkt_sasl_iov *vectors,
                                          size_t count, const char **output,
                                          unsigned long *output_length) {
  sasl_out_params_t *native = (sasl_out_params_t *)self->internal;
  struct iovec *iov;
  unsigned native_length = 0;
  size_t i;
  int status;
  if (native == NULL || native->encode == NULL || count > UINT_MAX ||
      count > ((size_t)-1) / sizeof(*iov) || (count != 0 && vectors == NULL))
    return SASL_BADPARAM;
  iov = count == 0 ? NULL : (struct iovec *)calloc(count, sizeof(*iov));
  if (count != 0 && iov == NULL)
    return SASL_NOMEM;
  for (i = 0; i < count; ++i) {
    iov[i].iov_base = (void *)vectors[i].data;
    iov[i].iov_len = vectors[i].byte_count;
  }
  status = native->encode(native->encode_context, iov, (unsigned)count, output,
                          &native_length);
  free(iov);
  if (output_length != NULL)
    *output_length = native_length;
  return status;
}

static int cpkt_sasl_plugin_native_decode(cpkt_sasl_plugin_output *self,
                                          const char *input,
                                          unsigned long input_length,
                                          const char **output,
                                          unsigned long *output_length) {
  sasl_out_params_t *native = (sasl_out_params_t *)self->internal;
  unsigned native_length = 0;
  int status;
  if (native == NULL || native->decode == NULL || input_length > UINT_MAX)
    return SASL_BADPARAM;
  status = native->decode(native->decode_context, input, (unsigned)input_length,
                          output, &native_length);
  if (output_length != NULL)
    *output_length = native_length;
  return status;
}

/** C89 facade contract for cpkt_sasl_plugin_output_from_native; see the public
 * header for ownership and callback lifetime. */
void cpkt_sasl_plugin_output_from_native(cpkt_sasl_plugin_output *out,
                                         sasl_out_params_t *native) {
  memset(out, 0, sizeof(*out));
  out->done = native->doneflag;
  out->user = native->user;
  out->authentication_identity = native->authid;
  out->user_length = native->ulen;
  out->authentication_length = native->alen;
  out->maximum_output_bytes = native->maxoutbuf;
  out->mechanism_ssf = native->mech_ssf;
  out->encode_context = native->encode_context;
  out->encode = native->encode == NULL ? NULL : cpkt_sasl_plugin_native_encode;
  out->decode_context = native->decode_context;
  out->decode = native->decode == NULL ? NULL : cpkt_sasl_plugin_native_decode;
  out->client_credentials = native->client_creds;
  out->gss_peer_name = (const cpkt_gss_name *)native->gss_peer_name;
  out->gss_local_name = (const cpkt_gss_name *)native->gss_local_name;
  out->channel_binding_name = native->cbindingname;
  out->channel_binding_disposition = native->cbindingdisp;
  out->parameter_version = native->param_version;
  out->internal = native;
}

static int cpkt_sasl_plugin_public_encode(void *context,
                                          const struct iovec *native_iov,
                                          unsigned count, const char **output,
                                          unsigned *output_length) {
  cpkt_sasl_plugin_output_bridge *bridge =
      (cpkt_sasl_plugin_output_bridge *)context;
  cpkt_sasl_iov *iov;
  unsigned long length = 0;
  unsigned i;
  int status;
  if (bridge == NULL || bridge->public_output.encode == NULL ||
      (count != 0 && native_iov == NULL))
    return SASL_BADPARAM;
  iov = count == 0 ? NULL : (cpkt_sasl_iov *)calloc(count, sizeof(*iov));
  if (count != 0 && iov == NULL)
    return SASL_NOMEM;
  for (i = 0; i < count; ++i) {
    iov[i].data = native_iov[i].iov_base;
    iov[i].byte_count = native_iov[i].iov_len;
  }
  status = bridge->public_output.encode(&bridge->public_output, iov, count,
                                        output, &length);
  free(iov);
  if (length > UINT_MAX)
    return SASL_BADPARAM;
  if (output_length != NULL)
    *output_length = (unsigned)length;
  return status;
}

static int cpkt_sasl_plugin_public_decode(void *context, const char *input,
                                          unsigned input_length,
                                          const char **output,
                                          unsigned *output_length) {
  cpkt_sasl_plugin_output_bridge *bridge =
      (cpkt_sasl_plugin_output_bridge *)context;
  unsigned long length = 0;
  int status;
  if (bridge == NULL || bridge->public_output.decode == NULL)
    return SASL_BADPARAM;
  status = bridge->public_output.decode(&bridge->public_output, input,
                                        input_length, output, &length);
  if (length > UINT_MAX)
    return SASL_BADPARAM;
  if (output_length != NULL)
    *output_length = (unsigned)length;
  return status;
}

/** C89 facade contract for cpkt_sasl_plugin_output_to_native; see the public
 * header for ownership and callback lifetime. */
int cpkt_sasl_plugin_output_to_native(const cpkt_sasl_plugin_output *out,
                                      sasl_out_params_t *native,
                                      cpkt_sasl_plugin_output_bridge *bridge) {
  if (out->done > UINT_MAX || out->user_length > UINT_MAX ||
      out->authentication_length > UINT_MAX ||
      out->maximum_output_bytes > UINT_MAX || out->mechanism_ssf > UINT_MAX ||
      out->channel_binding_disposition > UINT_MAX)
    return SASL_BADPARAM;
  if (bridge == NULL &&
      ((out->encode != NULL && out->encode != cpkt_sasl_plugin_native_encode) ||
       (out->decode != NULL && out->decode != cpkt_sasl_plugin_native_decode)))
    return SASL_BADPARAM;
  native->doneflag = (unsigned)out->done;
  native->user = out->user;
  native->authid = out->authentication_identity;
  native->ulen = (unsigned)out->user_length;
  native->alen = (unsigned)out->authentication_length;
  native->maxoutbuf = (unsigned)out->maximum_output_bytes;
  native->mech_ssf = (sasl_ssf_t)out->mechanism_ssf;
  native->client_creds = out->client_credentials;
  native->gss_peer_name = out->gss_peer_name;
  native->gss_local_name = out->gss_local_name;
  native->cbindingname = out->channel_binding_name;
  native->cbindingdisp = (unsigned)out->channel_binding_disposition;
  native->param_version = out->parameter_version;
  if (out->encode == NULL) {
    native->encode = NULL;
    native->encode_context = NULL;
  } else if (out->encode != cpkt_sasl_plugin_native_encode) {
    bridge->public_output = *out;
    bridge->public_output.internal = NULL;
    native->encode = cpkt_sasl_plugin_public_encode;
    native->encode_context = bridge;
  }
  if (out->decode == NULL) {
    native->decode = NULL;
    native->decode_context = NULL;
  } else if (out->decode != cpkt_sasl_plugin_native_decode) {
    bridge->public_output = *out;
    bridge->public_output.internal = NULL;
    native->decode = cpkt_sasl_plugin_public_decode;
    native->decode_context = bridge;
  }
  return SASL_OK;
}

/** C89 facade contract for cpkt_sasl_plugin_client_canonicalize; see the public
 * header for ownership and callback lifetime. */
int cpkt_sasl_plugin_client_canonicalize(cpkt_sasl_client_params *self,
                                         const char *input,
                                         unsigned long length,
                                         unsigned long flags,
                                         cpkt_sasl_plugin_output *output) {
  sasl_client_params_t *native =
      self == NULL ? NULL : (sasl_client_params_t *)self->internal;
  sasl_out_params_t native_output;
  int status;
  if (native == NULL || native->canon_user == NULL || length > UINT_MAX ||
      flags > UINT_MAX || output == NULL)
    return SASL_BADPARAM;
  memset(&native_output, 0, sizeof(native_output));
  if (output->user_length > UINT_MAX ||
      output->authentication_length > UINT_MAX)
    return SASL_BADPARAM;
  native_output.user = output->user;
  native_output.authid = output->authentication_identity;
  native_output.ulen = (unsigned)output->user_length;
  native_output.alen = (unsigned)output->authentication_length;
  status = native->canon_user(native->utils->conn, input, (unsigned)length,
                              (unsigned)flags, &native_output);
  if (status == SASL_OK) {
    output->user = native_output.user;
    output->authentication_identity = native_output.authid;
    output->user_length = native_output.ulen;
    output->authentication_length = native_output.alen;
  }
  return status;
}

/** C89 facade contract for cpkt_sasl_plugin_server_canonicalize; see the public
 * header for ownership and callback lifetime. */
int cpkt_sasl_plugin_server_canonicalize(cpkt_sasl_server_params *self,
                                         const char *input,
                                         unsigned long length,
                                         unsigned long flags,
                                         cpkt_sasl_plugin_output *output) {
  sasl_server_params_t *native =
      self == NULL ? NULL : (sasl_server_params_t *)self->internal;
  sasl_out_params_t native_output;
  int status;
  if (native == NULL || native->canon_user == NULL || length > UINT_MAX ||
      flags > UINT_MAX || output == NULL)
    return SASL_BADPARAM;
  memset(&native_output, 0, sizeof(native_output));
  if (output->user_length > UINT_MAX ||
      output->authentication_length > UINT_MAX)
    return SASL_BADPARAM;
  native_output.user = output->user;
  native_output.authid = output->authentication_identity;
  native_output.ulen = (unsigned)output->user_length;
  native_output.alen = (unsigned)output->authentication_length;
  status = native->canon_user(native->utils->conn, input, (unsigned)length,
                              (unsigned)flags, &native_output);
  if (status == SASL_OK) {
    output->user = native_output.user;
    output->authentication_identity = native_output.authid;
    output->user_length = native_output.ulen;
    output->authentication_length = native_output.alen;
  }
  return status;
}

/** C89 facade contract for cpkt_sasl_plugin_server_transition; see the public
 * header for ownership and callback lifetime. */
int cpkt_sasl_plugin_server_transition(cpkt_sasl_server_params *self,
                                       const char *password,
                                       unsigned long length) {
  sasl_server_params_t *native =
      self == NULL ? NULL : (sasl_server_params_t *)self->internal;
  if (native == NULL || native->transition == NULL || length > UINT_MAX)
    return SASL_BADPARAM;
  return native->transition(native->utils->conn, password, (unsigned)length);
}
