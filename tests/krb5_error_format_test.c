#include <krb5.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void prepend(krb5_context context, const char *format, ...) {
  va_list args;
  va_start(args, format);
  krb5_vprepend_error_message(context, EINVAL, format, args);
  va_end(args);
}

static int check_message(krb5_context context, const char *expected) {
  const char *actual = krb5_get_error_message(context, EINVAL);
  int matches = actual != NULL && strcmp(actual, expected) == 0;
  if (!matches)
    fprintf(stderr, "Kerberos message: got '%s', expected '%s'\n",
            actual == NULL ? "(null)" : actual, expected);
  if (actual != NULL)
    krb5_free_error_message(context, actual);
  return matches;
}

int main(void) {
  krb5_context context;
  int success;
  if (krb5_init_context(&context) != 0)
    return 2;
  krb5_set_error_message(context, EINVAL, "%s", "root cause");
  prepend(context, "code %d", 42);
  success = check_message(context, "code 42: root cause");
  if (success) {
    krb5_set_error_message(context, EINVAL, "%s", "root cause");
    prepend(context, "%s %ld %.2f", "detail", 123456L, 1.25);
    success = check_message(context, "detail 123456 1.25: root cause");
  }
  if (success) {
    krb5_prepend_error_message(context, EINVAL, "outer %d", 7);
    success = check_message(context, "outer 7: detail 123456 1.25: root cause");
  }
  krb5_free_context(context);
  return success ? 0 : 1;
}
