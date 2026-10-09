#include <cpkt/gssapi.h>
#include <krb5.h>

/* Internal provider entry point used by GSS context construction. */
extern krb5_error_code krb5_gss_apply_trace(krb5_context context);

static void trace_record(void *context, const char *message) {
  unsigned int *count = (unsigned int *)context;
  if (message != NULL && message[0] != '\0')
    ++*count;
}

int main(void) {
  krb5_context context = NULL;
  krb5_ccache cache = NULL;
  unsigned int count = 0;
  krb5_error_code error;

  cpkt_gss_set_trace_callback(trace_record, &count);
  error = krb5_init_context(&context);
  if (error != 0)
    return 1;
  error = krb5_gss_apply_trace(context);
  if (error == 0)
    error = krb5_cc_new_unique(context, "MEMORY", NULL, &cache);
  if (cache != NULL)
    (void)krb5_cc_destroy(context, cache);
  krb5_free_context(context);
  cpkt_gss_set_trace_callback(NULL, NULL);
  return error != 0 || count == 0 ? 2 : 0;
}
