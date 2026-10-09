#ifndef CPKT_OPCUA_LOGGING_PEER_H
#define CPKT_OPCUA_LOGGING_PEER_H
#include <cpkt/opcua.h>

/* Test-only adapter state; the C89 consumer never includes upstream headers. */
struct cpkt_log_sink {
  void *logger;
  size_t records;
  size_t bytes;
  size_t lines;
  size_t chunks;
  size_t z_count;
  char prefix[2048];
  size_t prefix_length;
  cpkt_opcua_log_level last_level;
  cpkt_opcua_log_category last_category;
};
struct cpkt_log_request {
  int kind;
  int plugin;
  cpkt_opcua_log_level level;
  cpkt_opcua_log_category category;
  const char *message;
  size_t length;
};
void cpkt_log_sink_init(struct cpkt_log_sink *sink);
void cpkt_log_sink_clear(struct cpkt_log_sink *sink);
void cpkt_log_sink_emit(struct cpkt_log_sink *sink,
                        const cpkt_opcua_log_record *record);
cpkt_opcua_status cpkt_log_server_emit(void *config, void *user);
cpkt_opcua_status cpkt_log_client_emit(void *config, void *user);
cpkt_opcua_status cpkt_log_server_reject_password(void *config, void *user);
cpkt_opcua_status cpkt_log_client_reject_password(void *config, void *user);
cpkt_opcua_status cpkt_log_server_owned_logger(void *config, void *user);
cpkt_opcua_status cpkt_log_client_owned_logger(void *config, void *user);
void cpkt_log_credentials(unsigned char **certificate,
                          size_t *certificate_length, unsigned char **key,
                          size_t *key_length);
void cpkt_log_credentials_free(unsigned char *certificate, unsigned char *key);
#ifdef CPKT_LOGGING_ALLOC_FAILURE_TEST
void cpkt_log_fail_allocation(size_t length);
int cpkt_log_allocation_failed(void);
cpkt_opcua_status cpkt_log_client_hkdf_failure(void *config, void *user);
#endif
#endif
