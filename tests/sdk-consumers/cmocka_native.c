#include <cmocka.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static uintmax_t native_queue(void) { return mock_uint(); }
static void native_behavior(void **state) {
  uintmax_t value = UINT64_C(0xffffffff87654321);
  (void)state;
  will_return_uint(native_queue, value);
  assert_uint_equal(native_queue(), value);
}
int main(void) {
  const struct CMUnitTest tests[] = {cmocka_unit_test(native_behavior)};
  return cmocka_run_group_tests(tests, NULL, NULL);
}
