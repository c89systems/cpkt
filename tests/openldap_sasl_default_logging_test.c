#define _POSIX_C_SOURCE 200809L

#include <ldap.h>
#include <sasl/sasl.h>

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int wrapped_calls;
static int log_callback_present;

static void capture_log(const char *message) { (void)message; }

int __wrap_sasl_client_new(const char *service, const char *server_fqdn,
                           const char *iplocalport, const char *ipremoteport,
                           const sasl_callback_t *callbacks, unsigned flags,
                           sasl_conn_t **connection_out) {
  const sasl_callback_t *callback;

  (void)service;
  (void)server_fqdn;
  (void)iplocalport;
  (void)ipremoteport;
  (void)flags;
  (void)connection_out;
  ++wrapped_calls;
  for (callback = callbacks;
       callback != NULL && callback->id != SASL_CB_LIST_END; ++callback) {
    if (callback->id == SASL_CB_LOG && callback->proc != NULL) {
      log_callback_present = 1;
    }
  }
  return SASL_FAIL;
}

static int no_interaction(LDAP *ldap, unsigned flags, void *defaults,
                          void *interact) {
  (void)ldap;
  (void)flags;
  (void)defaults;
  (void)interact;
  return LDAP_PARAM_ERROR;
}

int main(int argc, char **argv) {
  char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
  char url[512];
  struct sockaddr_un address;
  BER_LOG_PRINT_FN old_sink;
  LDAP *ldap = NULL;
  const char *mechanism = NULL;
  int custom;
  int fd;
  int msgid = -1;
  int protocol_version = LDAP_VERSION3;
  int result;

  if (argc != 2 ||
      (strcmp(argv[1], "default") != 0 && strcmp(argv[1], "custom") != 0))
    return 1;
  custom = strcmp(argv[1], "custom") == 0;
  /* Relative Unix socket names also work in long extracted build paths. */
  if (snprintf(path, sizeof(path), "cpkt-ldap-sasl-%ld", (long)getpid()) >=
      (int)sizeof(path))
    return 2;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  memcpy(address.sun_path, path, strlen(path) + 1U);
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return 3;
  (void)unlink(path);
  if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
      listen(fd, 1) != 0) {
    close(fd);
    return 4;
  }
  (void)snprintf(url, sizeof(url), "ldapi://%s", path);

  old_sink = ber_set_log_print_fn(custom ? capture_log : NULL);
  result = ldap_initialize(&ldap, url);
  if (result == LDAP_SUCCESS)
    result =
        ldap_set_option(ldap, LDAP_OPT_PROTOCOL_VERSION, &protocol_version);
  if (result == LDAP_SUCCESS)
    result = ldap_sasl_interactive_bind(ldap, NULL, "EXTERNAL", NULL, NULL,
                                        LDAP_SASL_AUTOMATIC, no_interaction,
                                        NULL, NULL, &mechanism, &msgid);
  if (ldap != NULL)
    (void)ldap_unbind_ext(ldap, NULL, NULL);
  (void)ber_set_log_print_fn(old_sink);
  close(fd);
  (void)unlink(path);

  if (wrapped_calls != 1 || log_callback_present != custom)
    return 5;
  return result == LDAP_LOCAL_ERROR ? 0 : 6;
}
