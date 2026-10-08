#define _POSIX_C_SOURCE 200809L

#include <ldap.h>
#include <sasl/sasl.h>

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static unsigned int sasl_status_count;

static void capture_log(const char *message) {
  if (message != NULL &&
      strstr(message, "SASL/EXTERNAL authentication started") != NULL)
    ++sasl_status_count;
}

static int answer_empty_user(LDAP *ldap, unsigned flags, void *defaults,
                             void *interact) {
  sasl_interact_t *prompt = (sasl_interact_t *)interact;

  (void)ldap;
  (void)flags;
  (void)defaults;
  if (prompt == NULL)
    return LDAP_PARAM_ERROR;
  for (; prompt->id != SASL_CB_LIST_END; ++prompt) {
    if (prompt->id != SASL_CB_USER)
      return LDAP_PARAM_ERROR;
    prompt->result = "";
    prompt->len = 0;
  }
  return LDAP_SUCCESS;
}

int main(void) {
  char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
  char url[512];
  struct sockaddr_un address;
  BER_LOG_PRINT_FN old_sink = NULL;
  LDAP *ldap = NULL;
  const char *mechanism = NULL;
  int fd;
  int msgid = -1;
  int result;
  int protocol_version = LDAP_VERSION3;

  /* Relative Unix socket names also work in long extracted build paths. */
  if (snprintf(path, sizeof(path), "cpkt-ldap-log-%ld", (long)getpid()) >=
      (int)sizeof(path))
    return 1;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  memcpy(address.sun_path, path, strlen(path) + 1U);
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return 2;
  (void)unlink(path);
  if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
      listen(fd, 1) != 0) {
    close(fd);
    return 3;
  }
  (void)snprintf(url, sizeof(url), "ldapi://%s", path);
  old_sink = ber_set_log_print_fn(capture_log);
  result = ldap_initialize(&ldap, url);
  if (result == LDAP_SUCCESS) {
    result =
        ldap_set_option(ldap, LDAP_OPT_PROTOCOL_VERSION, &protocol_version);
  }
  if (result == LDAP_SUCCESS) {
    result = ldap_sasl_interactive_bind(ldap, NULL, "EXTERNAL", NULL, NULL,
                                        LDAP_SASL_AUTOMATIC, answer_empty_user,
                                        NULL, NULL, &mechanism, &msgid);
    (void)ldap_unbind_ext(ldap, NULL, NULL);
  }
  (void)ber_set_log_print_fn(old_sink);
  close(fd);
  (void)unlink(path);
  return sasl_status_count == 1U ? 0 : 4;
}
