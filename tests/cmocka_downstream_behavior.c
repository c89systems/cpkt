#include <cpkt/cmocka.h>
#include <stdlib.h>
#include <string.h>

static int setups;
static int teardowns;
static int setup(void **state) {
  *state = &setups;
  ++setups;
  return 0;
}
static int teardown(void **state) {
  assert_ptr_equal(*state, &setups);
  ++teardowns;
  return 0;
}
static unsigned long mocked(int value) {
  check_expected_int(mocked, value);
  function_called(mocked);
  assert_true(has_mock(mocked));
  return mock_uint(mocked);
}
static long mocked_signed(void) { return mock_int(mocked_signed); }
static int *mocked_pointer(void) {
  return mock_ptr_type(mocked_pointer, int *);
}
static float mocked_float(void) { return mock_float(mocked_float); }
static double mocked_double(void) { return mock_double(mocked_double); }
static void behavioral(void **state) {
  long signed_set[] = {-7, 12};
  unsigned long unsigned_set[] = {7, 12};
  double float_set[] = {1.5, 2.5};
  long narrow_set[] = {250};
  int no_evaluation = 0;
  unsigned long wide = 123;
  long signed_wide = -123;
  assert_ptr_equal(*state, &setups);
  expect_int_value(mocked, value, -7);
  expect_function_call(mocked);
  will_return_uint(mocked, wide);
  assert_uint_equal(mocked(-7), wide);
  will_return_int(mocked_signed, signed_wide);
  assert_int_equal(mocked_signed(), signed_wide);
  will_return_ptr(mocked_pointer, &setups);
  assert_ptr_equal(mocked_pointer(), &setups);
  will_return_float(mocked_float, 1.5f);
  assert_float_equal(mocked_float(), 1.5, 0.0001);
  will_return_double(mocked_double, 2.5);
  assert_float_equal(mocked_double(), 2.5, 0.0001);
  assert_int_in_set(-7, signed_set, 2);
  assert_int_not_in_set(3, signed_set, 2);
  assert_uint_in_set(12, unsigned_set, 2);
  assert_uint_not_in_set(3, unsigned_set, 2);
  assert_float_in_set(1.5, float_set, 2, 0.001);
  assert_float_not_in_set(3.5, float_set, 2, 0.001);
  assert_int_in_set(250, narrow_set, 1);
  if (1)
    assert_int_in_set(-7, signed_set, 2);
  else
    no_evaluation++;
  assert_int_in_set(++no_evaluation, signed_set, 0);
  assert_int_equal(no_evaluation, 0);
  {
    CpktCmockaWords wide_words = cpkt_cmocka_words(256, 123);
    CpktCmockaWords signed_words = cpkt_cmocka_words(0xffffff00UL, 123);
    CpktCmockaWords set[2];
    CMockaValueData observed;
    set[0] = wide_words;
    set[1] = signed_words;
    cpkt_cmocka_will_return("wide_queue", __FILE__, __LINE__, NULL,
                            cpkt_cmocka_value_words(wide_words), 1);
    observed = cpkt_cmocka_mock("wide_queue", __FILE__, __LINE__, NULL);
    cpkt_cmocka_assert_uint_equal_words(observed.words, wide_words, __FILE__,
                                        __LINE__);
    cpkt_cmocka_assert_int_in_set_words(signed_words, set, 2, __FILE__,
                                        __LINE__);
  }
  {
    const size_t count = 2000000;
    unsigned long *large = calloc(count, sizeof(*large));
    CpktCmockaWords *large_words;
    assert_non_null(large);
    assert_uint_in_set(0, large, count);
    free(large);
    large_words = calloc(count, sizeof(*large_words));
    assert_non_null(large_words);
    cpkt_cmocka_assert_uint_in_set_words(cpkt_cmocka_words(0, 0), large_words,
                                         count, __FILE__, __LINE__);
    free(large_words);
  }
  assert_string_equal("fixture", "fixture");
}
static void intentional_failure(void **state) {
  (void)state;
  fail_msg("intentional failure %d", 37);
}
int main(int argc, char **argv) {
  const struct CMUnitTest passed[] = {
      cmocka_unit_test_setup_teardown(behavioral, setup, teardown)};
  const struct CMUnitTest failed[] = {cmocka_unit_test(intentional_failure)};
  int result;
  if (argc > 1 && strcmp(argv[1], "failure") == 0) {
    return cmocka_run_group_tests(failed, NULL, NULL);
  }
  result = cmocka_run_group_tests(passed, NULL, NULL);
  if (result == 0 && (setups != 1 || teardowns != 1)) {
    return 2;
  }
  return result;
}
