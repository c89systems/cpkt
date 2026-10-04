#include <libssh2.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

enum test_scenario { LARGE_FORWARD, SMALL_FORWARD, LARGE_BACKWARD };

static enum test_scenario scenario;
static int poll_calls;
static int clock_calls;

int __wrap_poll(struct pollfd *fds, nfds_t count, int timeout) {
  int expected;
  (void)fds;
  if (count != 0 || ++poll_calls > 2) {
    fprintf(stderr, "scenario=%d poll count=%lu calls=%d clock=%d\n",
            (int)scenario, (unsigned long)count, poll_calls, clock_calls);
    exit(31);
  }
  expected = scenario == SMALL_FORWARD && poll_calls == 2 ? 90 : 100;
  if (timeout != expected) {
    fprintf(stderr, "scenario=%d poll=%d timeout=%d expected=%d\n",
            (int)scenario, poll_calls, timeout, expected);
    exit(32);
  }
  return 0;
}

#ifdef CPKT_LIBSSH2_TEST_MUSL_TIME64
int __wrap___gettimeofday_time64(struct timeval *value, void *zone) {
#else
int __wrap_gettimeofday(struct timeval *value, void *zone) {
#endif
  long seconds = 1700000000L;
  long microseconds = 0;
  (void)zone;
  ++clock_calls;
  if (clock_calls > 4)
    exit(33);
  if (scenario == LARGE_FORWARD && clock_calls >= 2)
    seconds += 30L * 24L * 60L * 60L;
  if (scenario == LARGE_BACKWARD && clock_calls == 1)
    seconds += 30L * 24L * 60L * 60L;
  if (scenario == LARGE_BACKWARD && clock_calls == 4)
    microseconds = 100000;
  if (scenario == SMALL_FORWARD) {
    if (clock_calls == 2 || clock_calls == 3)
      microseconds = 10000;
    if (clock_calls == 4)
      microseconds = 100000;
  }
  value->tv_sec = (time_t)seconds;
  value->tv_usec = (suseconds_t)microseconds;
  return 0;
}

static int check(enum test_scenario selected, int expected_polls) {
  scenario = selected;
  poll_calls = 0;
  clock_calls = 0;
  if (libssh2_poll(NULL, 0, 100) != 0 || poll_calls != expected_polls ||
      clock_calls != expected_polls * 2) {
    fprintf(stderr, "libssh2_poll elapsed-time scenario %d failed\n",
            (int)selected);
    return 1;
  }
  return 0;
}

int main(void) {
  if (check(LARGE_FORWARD, 1) || check(SMALL_FORWARD, 2) ||
      check(LARGE_BACKWARD, 2))
    return 1;
  return 0;
}
