#ifndef CPKT_SASL_PRIVATE_H
#define CPKT_SASL_PRIVATE_H

#include <cpkt/sasl.h>
#include <cpkt/sasl_plugin.h>
#include <sasl/prop.h>
#include <sasl/sasl.h>
#include <sasl/saslplug.h>

struct cpkt_sasl_property_context {
  struct propctx *native;
  cpkt_sasl_property_value *views;
  size_t view_count;
  size_t view_capacity;
  int borrowed;
};

typedef struct cpkt_sasl_plugin_server_view {
  cpkt_sasl_server_params params;
  cpkt_sasl_plugin_utils utils;
  cpkt_sasl_channel_binding binding;
  cpkt_sasl_http_request request;
  cpkt_sasl_property_context properties;
} cpkt_sasl_plugin_server_view;

typedef struct cpkt_sasl_plugin_client_view {
  cpkt_sasl_client_params params;
  cpkt_sasl_plugin_utils utils;
  cpkt_sasl_channel_binding binding;
  cpkt_sasl_http_request request;
} cpkt_sasl_plugin_client_view;

typedef struct cpkt_sasl_plugin_output_bridge {
  cpkt_sasl_plugin_output public_output;
} cpkt_sasl_plugin_output_bridge;

__attribute__((visibility("hidden"))) void
cpkt_sasl_property_discard_borrowed_views(cpkt_sasl_property_context *context);
__attribute__((visibility("hidden"))) cpkt_sasl *
cpkt_sasl_public_for_native(sasl_conn_t *native);
__attribute__((visibility("hidden"))) sasl_conn_t *
cpkt_sasl_native_connection(const cpkt_sasl *self);
__attribute__((visibility("hidden"))) void *
cpkt_sasl_option_application_context(const cpkt_sasl *self);
__attribute__((visibility("hidden"))) void *
cpkt_sasl_global_option_application_context(int is_server);
__attribute__((visibility("hidden"))) void
cpkt_sasl_plugin_utils_set_global_option_context(const sasl_utils_t *native,
                                                 void *context);
__attribute__((visibility("hidden"))) int
cpkt_sasl_connection_set_text(cpkt_sasl *self, int property, const char *value);
__attribute__((visibility("hidden"))) int
cpkt_sasl_connection_get_security(const cpkt_sasl *self,
                                  cpkt_sasl_security_properties *out);
__attribute__((visibility("hidden"))) int
cpkt_sasl_connection_get_http(const cpkt_sasl *self,
                              cpkt_sasl_http_request *out);
__attribute__((visibility("hidden"))) int
cpkt_sasl_connection_get_callbacks(const cpkt_sasl *self,
                                   const cpkt_sasl_callbacks **out);
__attribute__((visibility("hidden"))) int
cpkt_sasl_connection_get_option_context(const cpkt_sasl *self, void **out);
__attribute__((visibility("hidden"))) const cpkt_sasl_callbacks *
cpkt_sasl_callbacks_from_native_context(void *context);
__attribute__((visibility("hidden"))) void
cpkt_sasl_plugin_utils_initialize(cpkt_sasl_plugin_utils *public_utils,
                                  const sasl_utils_t *native);
__attribute__((visibility("hidden"))) cpkt_sasl_plugin_utils *
cpkt_sasl_plugin_utils_for_native(const sasl_utils_t *native);
__attribute__((visibility("hidden"))) void
cpkt_sasl_plugin_utils_forget_connection(sasl_conn_t *native);
__attribute__((visibility("hidden"))) void cpkt_sasl_plugin_utils_cleanup(void);
__attribute__((visibility("hidden"))) void
cpkt_sasl_auxiliary_plugins_cleanup(void);
__attribute__((visibility("hidden"))) void
cpkt_sasl_canonicalizers_cleanup(void);
__attribute__((visibility("hidden"))) void
cpkt_sasl_client_plugins_cleanup(void);
__attribute__((visibility("hidden"))) void
cpkt_sasl_server_plugins_cleanup(void);
__attribute__((visibility("hidden"))) const cpkt_sasl_client_plugin *
cpkt_sasl_client_plugin_for_native(const sasl_client_plug_t *native);
__attribute__((visibility("hidden"))) const cpkt_sasl_server_plugin *
cpkt_sasl_server_plugin_for_native(const sasl_server_plug_t *native);
__attribute__((visibility("hidden"))) const cpkt_sasl_auxiliary_plugin *
cpkt_sasl_auxiliary_plugin_for_native(const sasl_auxprop_plug_t *native);
__attribute__((visibility("hidden"))) void
cpkt_sasl_plugin_server_params_from_native(cpkt_sasl_plugin_server_view *view,
                                           sasl_server_params_t *native);
__attribute__((visibility("hidden"))) void
cpkt_sasl_plugin_client_params_from_native(cpkt_sasl_plugin_client_view *view,
                                           sasl_client_params_t *native);
__attribute__((visibility("hidden"))) void
cpkt_sasl_plugin_output_from_native(cpkt_sasl_plugin_output *out,
                                    sasl_out_params_t *native);
__attribute__((visibility("hidden"))) int
cpkt_sasl_plugin_output_to_native(const cpkt_sasl_plugin_output *out,
                                  sasl_out_params_t *native,
                                  cpkt_sasl_plugin_output_bridge *bridge);
__attribute__((visibility("hidden"))) int cpkt_sasl_plugin_client_canonicalize(
    cpkt_sasl_client_params *self, const char *input, unsigned long length,
    unsigned long flags, cpkt_sasl_plugin_output *output);
__attribute__((visibility("hidden"))) int cpkt_sasl_plugin_server_canonicalize(
    cpkt_sasl_server_params *self, const char *input, unsigned long length,
    unsigned long flags, cpkt_sasl_plugin_output *output);
__attribute__((visibility("hidden"))) int
cpkt_sasl_plugin_server_transition(cpkt_sasl_server_params *self,
                                   const char *password, unsigned long length);

#endif
