#ifndef CPKT_OPCUA_TYPES_PEER_H
#define CPKT_OPCUA_TYPES_PEER_H
#include <stddef.h>
void cpkt_types_peer_configuration(void);
void cpkt_types_peer_log(void *client);
void cpkt_types_peer_accesscontrol_allocations(void);
unsigned int cpkt_types_peer_attribute_borrow(void *owner, const void *view,
                                              int client);
unsigned int cpkt_types_peer_format(unsigned int kind, unsigned char **bytes,
                                    size_t *length);
void cpkt_types_test_memory_backend(void);
size_t cpkt_types_peer_stock_remove_end(void);
unsigned int cpkt_types_peer_stock_history(void *native_server);
unsigned int cpkt_types_peer_custom_gathering(void *native_server,
                                              const void *owned_setting);
int cpkt_types_peer_stock_borrow(const void *stored, const unsigned char *text,
                                 size_t length, unsigned int timestamp);
unsigned int cpkt_types_native_empty(size_t index, unsigned char **bytes,
                                     size_t *length);
unsigned int cpkt_types_native_populated(size_t index, unsigned char **bytes,
                                         size_t *length);
void cpkt_types_native_free(unsigned char *bytes);
void *cpkt_types_peer_start(unsigned short *port);
int cpkt_types_peer_check(void *peer);
unsigned int cpkt_types_peer_highlevel_setup(void *peer);
void cpkt_types_peer_highlevel_history_counts(void *peer, unsigned int *pages,
                                              unsigned int *releases);
unsigned int cpkt_types_peer_async_add_output(void *client);
unsigned int cpkt_types_peer_client_timeout(void *client, unsigned int timeout);
void cpkt_types_peer_pause(void *peer, int pause);
void cpkt_types_peer_client_fail_conversion(int kind, size_t count);
unsigned int cpkt_types_peer_event(void *peer);
unsigned int cpkt_types_peer_defaults(int monitored, unsigned char **bytes,
                                      size_t *length);
unsigned int cpkt_types_peer_single_error(void *client,
                                          unsigned int subscription_id,
                                          int events, int missing,
                                          unsigned int *deleted);
unsigned int cpkt_types_peer_access_control(void *native_server);
unsigned int cpkt_types_peer_history(void *native_server);
unsigned int cpkt_types_peer_history_failure(void *native_server);
unsigned int cpkt_types_peer_close_session_failure(void *native_server,
                                                   void *context);
unsigned int cpkt_types_peer_history_backend(void *native_server, int mode);
unsigned int cpkt_types_peer_history_poll_node(void *native_server);
struct cpkt_async_peer {
  int deferred;
  int cancelled;
  void *read_result;
  const void *write_result;
  void *call_result;
};
unsigned int cpkt_types_peer_async_install(void *server, void *state);
unsigned int cpkt_types_peer_async_complete(void *server, void *state);
unsigned int cpkt_types_peer_variant_predicates(size_t type, size_t query,
                                                size_t length, int state);
unsigned int cpkt_types_peer_extension_predicate(int encoding, size_t type,
                                                 size_t query, int populated);
unsigned int cpkt_types_peer_bytes_hash(unsigned int initial,
                                        const unsigned char *bytes,
                                        size_t length);
unsigned int cpkt_types_peer_status_predicates(unsigned int status);
struct cpkt_identifier_peer {
  unsigned int parse_status, print_status, hash;
  int predicate, order;
  unsigned char output[256];
  size_t output_length;
  unsigned char *encoded;
  size_t encoded_length;
};
void cpkt_types_peer_identifier(int kind, int extended, const char *text,
                                size_t capacity,
                                struct cpkt_identifier_peer *result);
void cpkt_types_peer_offset(unsigned int *high, unsigned int *low);
void cpkt_types_peer_random(unsigned int high, unsigned int low,
                            unsigned char *guid, unsigned int *number);
void cpkt_types_peer_time(unsigned int high, unsigned int low,
                          unsigned int *unix_high, unsigned int *unix_low,
                          unsigned short *fields, short *year);
unsigned int cpkt_types_peer_nodes(void *server, int action, void *context);
unsigned int cpkt_types_peer_nodes_parallel(void *first, void *second);
struct cpkt_producer_observed {
  const void *address;
  unsigned int high[2], low[2], borrowed;
};
unsigned int
cpkt_types_peer_producer_direct(void *server, int kind, void *context,
                                struct cpkt_producer_observed *out);
void cpkt_types_peer_maps(unsigned int *results);
void cpkt_types_peer_stop(void *peer);
unsigned int cpkt_types_peer_producer_zero_methods(void *server, void *context);
struct cpkt_creation_observed {
  unsigned int status;
  int assigned, exists;
};
void cpkt_types_peer_creation_failure(int kind,
                                      struct cpkt_creation_observed *out);
struct cpkt_external_observed {
  const void *slot, *storage, *data;
  unsigned int high[2], low[2];
};
unsigned int cpkt_types_peer_external(void *server, unsigned int id,
                                      struct cpkt_external_observed *out);
void cpkt_types_peer_state(void *client, unsigned int *values);
double cpkt_types_peer_monotonic(void);
unsigned int cpkt_types_peer_session_endpoint(void *recipient, void *donor,
                                              const char *url);
unsigned int cpkt_types_peer_discovery(void *client, const char *url, int kind,
                                       unsigned char **bytes, size_t *length,
                                       size_t *count);
unsigned int cpkt_types_peer_username_status(const char *url);
unsigned int cpkt_types_peer_delete_timer(const char *url, unsigned int *calls);
void *cpkt_types_peer_external_loop(void *client, unsigned int *calls);
void cpkt_types_peer_external_loop_free(void *loop);
unsigned short cpkt_types_peer_port(void);
unsigned int cpkt_types_peer_reverse(void *peer, unsigned short port,
                                     int remove);
struct cpkt_value_factory_observed {
  unsigned char *bytes;
  size_t length, type;
  unsigned int status, aliases;
  int injected;
};
void cpkt_types_peer_value_factory(int kind, char *first, char *second,
                                   int stop_failure,
                                   struct cpkt_value_factory_observed *out);
struct cpkt_range_observed {
  unsigned int status, bounds[64];
  size_t count;
  int injected;
};
void cpkt_types_peer_range(const char *input, size_t length, int shorthand,
                           int stop_failure, struct cpkt_range_observed *out);
struct cpkt_endpoint_observed {
  unsigned int status;
  const unsigned char *host, *path;
  size_t host_length, path_length;
  unsigned short port;
  unsigned char pcp;
};
void cpkt_types_peer_endpoint(const char *input, size_t length, char *seed,
                              int ethernet, int path,
                              struct cpkt_endpoint_observed *out);
size_t cpkt_types_peer_number(const unsigned char *bytes, size_t length,
                              unsigned int *number, unsigned char base,
                              int decimal);
int cpkt_types_peer_constant_equal(const void *first, const void *second,
                                   size_t length);
unsigned int cpkt_types_peer_certificate(const void *, size_t, const void *,
                                         size_t, unsigned int, unsigned int *,
                                         unsigned int *, size_t *);
unsigned int cpkt_types_peer_decrypted_key(const void *, size_t, const void *,
                                           size_t, const void *, size_t);
unsigned int cpkt_types_peer_trust(unsigned int, unsigned int, size_t *,
                                   unsigned int *);
int cpkt_types_peer_nodepointer(const void *pointer, const void *id);
void cpkt_types_peer_node_copy(void);
#endif
