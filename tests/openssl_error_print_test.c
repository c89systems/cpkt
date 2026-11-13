/* Error printing must drain existing errors when further allocations fail. */
#include <openssl/crypto.h>
#include <openssl/err.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct capture {
  char lines[2][4096];
  unsigned int count;
  unsigned int stop_after;
  int invalid;
};

static int allocation_mode;
static unsigned int allocation_calls;
static unsigned int allocation_failures;

static int allocation_fails(void) {
  if (allocation_mode == 0)
    return 0;
  ++allocation_calls;
  if (allocation_mode == 2 || allocation_failures == 0) {
    ++allocation_failures;
    return 1;
  }
  return 0;
}

static void *test_malloc(size_t size, const char *file, int line) {
  (void)file;
  (void)line;
  return allocation_fails() ? NULL : malloc(size);
}

static void *test_realloc(void *address, size_t size, const char *file,
                          int line) {
  (void)file;
  (void)line;
  return allocation_fails() ? NULL : realloc(address, size);
}

static void test_free(void *address, const char *file, int line) {
  (void)file;
  (void)line;
  free(address);
}

static int capture_line(const char *text, size_t length, void *context) {
  struct capture *capture = context;
  /* Stop unexpected replacement errors without hanging the regression. */
  if (capture->count >= 2 || length >= sizeof(capture->lines[0])) {
    capture->invalid = 1;
    return 0;
  }
  memcpy(capture->lines[capture->count], text, length);
  capture->lines[capture->count][length] = '\0';
  ++capture->count;
  return capture->stop_after == 0 || capture->count < capture->stop_after;
}

static void seed_errors(void) {
  ERR_clear_error();
  ERR_raise_data(ERR_LIB_USER, 101, "%s", "first diagnostic");
  ERR_raise_data(ERR_LIB_USER, 102, "%s", "second diagnostic");
}

static int check_failure_mode(const struct capture *expected,
                              struct capture *actual, int mode) {
  unsigned int i;
  seed_errors();
  memset(actual, 0, sizeof(*actual));
  allocation_calls = 0;
  allocation_failures = 0;
  allocation_mode = mode;
  ERR_print_errors_cb(capture_line, actual);
  allocation_mode = 0;
  printf("mode=%d records=%u allocation_calls=%u allocation_failures=%u\n",
         mode, actual->count, allocation_calls, allocation_failures);
  if (actual->invalid || actual->count != expected->count ||
      ERR_peek_error() != 0)
    return 0;
  for (i = 0; i < expected->count; ++i) {
    if (strcmp(actual->lines[i], expected->lines[i]) != 0)
      return 0;
  }
  return 1;
}

static void *worker(void *context) {
  int *status = context;
  struct capture *captures = calloc(2, sizeof(*captures));
  struct capture *expected;
  struct capture *actual;
  CRYPTO_THREAD_ID tid = CRYPTO_THREAD_get_current_id();
  char prefix[2 * sizeof(tid) + 2];
  int mode;

  *status = 1;
  if (captures == NULL)
    return NULL;
  expected = &captures[0];
  actual = &captures[1];
  if (!OPENSSL_buf2hexstr_ex(prefix, sizeof(prefix), NULL,
                             (const unsigned char *)&tid, sizeof(tid), '\0'))
    goto end;
  prefix[2 * sizeof(tid)] = ':';
  prefix[2 * sizeof(tid) + 1] = '\0';

  seed_errors();
  ERR_print_errors_cb(capture_line, expected);
  if (expected->invalid || expected->count != 2 || ERR_peek_error() != 0 ||
      strncmp(expected->lines[0], prefix, strlen(prefix)) != 0 ||
      strncmp(expected->lines[1], prefix, strlen(prefix)) != 0 ||
      strstr(expected->lines[0], "first diagnostic") == NULL ||
      strstr(expected->lines[1], "second diagnostic") == NULL)
    goto end;

  *status = 0;
  for (mode = 1; mode <= 2; ++mode) {
    if (!check_failure_mode(expected, actual, mode)) {
      fprintf(stderr, "error printing failed under allocation mode %d\n", mode);
      *status = 1;
    }
  }

  seed_errors();
  memset(actual, 0, sizeof(*actual));
  actual->stop_after = 1;
  allocation_mode = 2;
  ERR_print_errors_cb(capture_line, actual);
  allocation_mode = 0;
  if (actual->invalid || actual->count != 1 ||
      strcmp(actual->lines[0], expected->lines[0]) != 0 ||
      ERR_GET_REASON(ERR_get_error()) != 102 || ERR_get_error() != 0)
    *status = 1;

  ERR_clear_error();
  ERR_raise(ERR_LIB_USER, 103);
  if (ERR_GET_REASON(ERR_get_error()) != 103 || ERR_get_error() != 0)
    *status = 1;
end:
  allocation_mode = 0;
  OPENSSL_thread_stop();
  free(captures);
  return NULL;
}

int main(void) {
  pthread_t thread;
  int status;
  if (!CRYPTO_set_mem_functions(test_malloc, test_realloc, test_free) ||
      pthread_create(&thread, NULL, worker, &status) != 0 ||
      pthread_join(thread, NULL) != 0)
    return 2;
  OPENSSL_cleanup();
  return status;
}
