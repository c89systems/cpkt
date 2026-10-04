#include <cpkt/sasl.h>

#include <string.h>

#include <sasl/sasl.h>

#include "sasl_private.h"

/** Dispatches only native copied-text property setters. Native SASL decides
 * which properties permit null clearing and which require a server receiver. */
int cpkt_sasl_connection_set_text(cpkt_sasl *self, int property,
                                  const char *value) {
  sasl_conn_t *native = cpkt_sasl_native_connection(self);
  if (native == NULL)
    return SASL_BADPARAM;
  switch (property) {
  case SASL_AUTH_EXTERNAL:
  case SASL_DEFUSERREALM:
  case SASL_APPNAME:
  case SASL_IPLOCALPORT:
  case SASL_IPREMOTEPORT:
    return sasl_setprop(native, property, value);
  default:
    return SASL_BADPARAM;
  }
}

/** Copies the native security record; nested name/value arrays remain
 * borrowed from the receiver until its security properties change. */
int cpkt_sasl_connection_get_security(const cpkt_sasl *self,
                                      cpkt_sasl_security_properties *out) {
  sasl_conn_t *native = cpkt_sasl_native_connection(self);
  const void *value = NULL;
  const sasl_security_properties_t *properties;
  int status;
  if (out != NULL)
    memset(out, 0, sizeof(*out));
  if (native == NULL || out == NULL)
    return SASL_BADPARAM;
  status = sasl_getprop(native, SASL_SEC_PROPS, &value);
  if (status != SASL_OK)
    return status;
  if (value == NULL)
    return SASL_NOTDONE;
  properties = (const sasl_security_properties_t *)value;
  out->minimum_ssf = properties->min_ssf;
  out->maximum_ssf = properties->max_ssf;
  out->maximum_buffer_bytes = properties->maxbufsize;
  out->security_flags = properties->security_flags;
  out->property_names = (const char *const *)properties->property_names;
  out->property_values = (const char *const *)properties->property_values;
  return SASL_OK;
}

/** Copies the public HTTP record while preserving borrowed nested byte
 * ranges. A null provider record produces a zero-filled output. */
int cpkt_sasl_connection_get_http(const cpkt_sasl *self,
                                  cpkt_sasl_http_request *out) {
  sasl_conn_t *native = cpkt_sasl_native_connection(self);
  const void *value = NULL;
  const sasl_http_request_t *request;
  int status;
  if (out != NULL)
    memset(out, 0, sizeof(*out));
  if (native == NULL || out == NULL)
    return SASL_BADPARAM;
  status = sasl_getprop(native, SASL_HTTP_REQUEST, &value);
  if (status != SASL_OK || value == NULL)
    return status;
  request = (const sasl_http_request_t *)value;
  out->method = request->method;
  out->uri = request->uri;
  out->entity = request->entity;
  out->entity_byte_count = request->elen;
  out->non_persistent = request->non_persist;
  return SASL_OK;
}

/** Returns the receiver's borrowed application callback table, if supplied;
 * it remains valid only while the receiver remains open. */
int cpkt_sasl_connection_get_callbacks(const cpkt_sasl *self,
                                       const cpkt_sasl_callbacks **out) {
  sasl_conn_t *native = cpkt_sasl_native_connection(self);
  const void *value = NULL;
  const sasl_callback_t *selected;
  int status;
  if (out != NULL)
    *out = NULL;
  if (native == NULL || out == NULL)
    return SASL_BADPARAM;
  status = sasl_getprop(native, SASL_CALLBACK, &value);
  if (status != SASL_OK || value == NULL)
    return status;
  selected = (const sasl_callback_t *)value;
  if (selected[0].id == SASL_CB_LIST_END)
    return SASL_OK;
  *out = cpkt_sasl_callbacks_from_native_context(selected[0].context);
  return *out == NULL ? SASL_FAIL : SASL_OK;
}

/** Maps native GETOPTCTX to the configured application option context.
 * Native connection and callback-owner pointers are never returned. */
int cpkt_sasl_connection_get_option_context(const cpkt_sasl *self, void **out) {
  sasl_conn_t *native = cpkt_sasl_native_connection(self);
  const void *value = NULL;
  const cpkt_sasl_callbacks *selected;
  int status;
  if (out != NULL)
    *out = NULL;
  if (native == NULL || out == NULL)
    return SASL_BADPARAM;
  status = sasl_getprop(native, SASL_GETOPTCTX, &value);
  if (status != SASL_OK)
    return status;
  selected = cpkt_sasl_callbacks_from_native_context((void *)value);
  if (selected != NULL)
    *out = selected->context;
  else if (value == native)
    *out = cpkt_sasl_option_application_context(self);
  return SASL_OK;
}
