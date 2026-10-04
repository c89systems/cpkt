#include <gssapi/gssapi.h>
#include <sasl/sasl.h>

#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int simulate_privileged_process;

uid_t getuid(void) { return (uid_t)1000; }
uid_t geteuid(void) {
  return simulate_privileged_process ? (uid_t)0 : (uid_t)1000;
}
gid_t getgid(void) { return (gid_t)1000; }
gid_t getegid(void) { return (gid_t)1000; }

int main(int argc, char **argv) {
  OM_uint32 minor_status;
  gss_buffer_desc empty_buffer;
  sasl_conn_t *connection;
  const char *mechanisms;
  unsigned int mechanisms_length;
  int mechanisms_count;
  int status;
  int has_gssapi;

  if (argc != 2 ||
      (strcmp(argv[1], "privileged") != 0 && strcmp(argv[1], "ordinary") != 0))
    return 1;
  simulate_privileged_process = strcmp(argv[1], "privileged") == 0;
  empty_buffer.length = 0;
  empty_buffer.value = NULL;
  if (gss_release_buffer(&minor_status, &empty_buffer) != GSS_S_COMPLETE)
    return 2;
  if (sasl_client_init(NULL) != SASL_OK)
    return 3;
  connection = NULL;
  status = sasl_client_new("cpkt-path-test", "localhost", NULL, NULL, NULL, 0,
                           &connection);
  if (status != SASL_OK || connection == NULL) {
    sasl_client_done();
    return 4;
  }
  mechanisms = NULL;
  mechanisms_length = 0;
  mechanisms_count = 0;
  status = sasl_listmech(connection, NULL, NULL, " ", NULL, &mechanisms,
                         &mechanisms_length, &mechanisms_count);
  has_gssapi = status == SASL_OK && mechanisms != NULL &&
               strstr(mechanisms, "GSSAPI") != NULL;
  sasl_dispose(&connection);
  sasl_client_done();
  if (simulate_privileged_process)
    return has_gssapi ? 0 : 5;
  return has_gssapi ? 6 : 0;
}
