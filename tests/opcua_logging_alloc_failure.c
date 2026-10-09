#include "opcua_logging_peer.h"

/* Link wrapping exercises the actual static library's allocator failure path,
 * without changing shipped library sources or upstream allocator settings. */
static size_t fail_length;
static int failed;
void *__real_calloc(size_t count, size_t length);
void *__wrap_calloc(size_t count, size_t length);

void cpkt_log_fail_allocation(size_t length) {
  fail_length = length;
  failed = 0;
}
int cpkt_log_allocation_failed(void) { return failed; }
void *__wrap_calloc(size_t count, size_t length) {
  if (fail_length != 0 && count == 1 && length == fail_length) {
    fail_length = 0;
    failed = 1;
    return NULL;
  }
  return __real_calloc(count, length);
}
