#include <cpkt/sasl_plugin.h>

#include <string.h>

#include <sasl/sasl.h>
#include <sasl/saslplug.h>

#include "sasl_private.h"

typedef struct cpkt_sasl_info_context {
  cpkt_sasl_plugin_info_callback callback;
  void *context;
} cpkt_sasl_info_context;

static void cpkt_sasl_client_info_native(client_sasl_mechanism_t *native,
                                         sasl_info_callback_stage_t stage,
                                         void *rock) {
  cpkt_sasl_info_context *ctx = (cpkt_sasl_info_context *)rock;
  cpkt_sasl_plugin_info info;
  if (native == NULL) {
    ctx->callback(ctx->context, NULL, stage);
    return;
  }
  memset(&info, 0, sizeof(info));
  info.version = native->version;
  info.plugin_name = native->plugname;
  if (native->plug != NULL) {
    info.mechanism_name = native->plug->mech_name;
    info.maximum_ssf = native->plug->max_ssf;
    info.security_flags = native->plug->security_flags;
    info.features = native->plug->features;
    info.required_prompts = native->plug->required_prompts;
    info.client_plugin = cpkt_sasl_client_plugin_for_native(native->plug);
  }
  ctx->callback(ctx->context, &info, stage);
}

static void cpkt_sasl_server_info_native(server_sasl_mechanism_t *native,
                                         sasl_info_callback_stage_t stage,
                                         void *rock) {
  cpkt_sasl_info_context *ctx = (cpkt_sasl_info_context *)rock;
  cpkt_sasl_plugin_info info;
  if (native == NULL) {
    ctx->callback(ctx->context, NULL, stage);
    return;
  }
  memset(&info, 0, sizeof(info));
  info.version = native->version;
  info.condition = native->condition;
  info.plugin_name = native->plugname;
  info.source_file = native->f;
  if (native->plug != NULL) {
    info.mechanism_name = native->plug->mech_name;
    info.maximum_ssf = native->plug->max_ssf;
    info.security_flags = native->plug->security_flags;
    info.features = native->plug->features;
    info.server_plugin = cpkt_sasl_server_plugin_for_native(native->plug);
  }
  ctx->callback(ctx->context, &info, stage);
}

static void cpkt_sasl_aux_info_native(sasl_auxprop_plug_t *native,
                                      sasl_info_callback_stage_t stage,
                                      void *rock) {
  cpkt_sasl_info_context *ctx = (cpkt_sasl_info_context *)rock;
  cpkt_sasl_plugin_info info;
  if (native == NULL) {
    ctx->callback(ctx->context, NULL, stage);
    return;
  }
  memset(&info, 0, sizeof(info));
  info.mechanism_name = native->name;
  info.features = native->features;
  info.auxiliary_plugin = cpkt_sasl_auxiliary_plugin_for_native(native);
  ctx->callback(ctx->context, &info, stage);
}

/** C89 facade contract for cpkt_sasl_client_plugin_info; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_client_plugin_info(const char *mechanisms,
                                 cpkt_sasl_plugin_info_callback callback,
                                 void *context) {
  cpkt_sasl_info_context ctx;
  if (callback == NULL)
    return SASL_BADPARAM;
  ctx.callback = callback;
  ctx.context = context;
  return sasl_client_plugin_info(mechanisms, cpkt_sasl_client_info_native,
                                 &ctx);
}

/** C89 facade contract for cpkt_sasl_server_plugin_info; see the public header
 * for ownership and callback lifetime. */
int cpkt_sasl_server_plugin_info(const char *mechanisms,
                                 cpkt_sasl_plugin_info_callback callback,
                                 void *context) {
  cpkt_sasl_info_context ctx;
  if (callback == NULL)
    return SASL_BADPARAM;
  ctx.callback = callback;
  ctx.context = context;
  return sasl_server_plugin_info(mechanisms, cpkt_sasl_server_info_native,
                                 &ctx);
}

/** C89 facade contract for cpkt_sasl_auxiliary_plugin_info; see the public
 * header for ownership and callback lifetime. */
int cpkt_sasl_auxiliary_plugin_info(const char *mechanisms,
                                    cpkt_sasl_plugin_info_callback callback,
                                    void *context) {
  cpkt_sasl_info_context ctx;
  if (callback == NULL)
    return SASL_BADPARAM;
  ctx.callback = callback;
  ctx.context = context;
  return auxprop_plugin_info(mechanisms, cpkt_sasl_aux_info_native, &ctx);
}
