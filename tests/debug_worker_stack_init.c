#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

static void stack_setup_check(int status) {
  if (status != 0) {
    fprintf(stderr, "Debug worker-stack setup failed: %d\n", status);
    /* A startup failure must also fail tests marked WILL_FAIL. */
    abort();
  }
}

__attribute__((constructor(101))) static void stack_setup(void) {
  pthread_attr_t attributes;
  struct rlimit before, after;
  size_t size, guard;
  long page_size;
  stack_setup_check(getrlimit(RLIMIT_STACK, &before));
  page_size = sysconf(_SC_PAGESIZE);
  stack_setup_check(page_size > 0 ? 0 : -1);
  stack_setup_check(pthread_getattr_default_np(&attributes));
  stack_setup_check(pthread_attr_setstacksize(&attributes, 128UL * 1024));
  stack_setup_check(pthread_attr_getguardsize(&attributes, &guard));
  if (guard < (size_t)page_size)
    stack_setup_check(
        pthread_attr_setguardsize(&attributes, (size_t)page_size));
  stack_setup_check(pthread_setattr_default_np(&attributes));
  stack_setup_check(pthread_attr_destroy(&attributes));
  stack_setup_check(pthread_getattr_default_np(&attributes));
  stack_setup_check(pthread_attr_getstacksize(&attributes, &size));
  stack_setup_check(pthread_attr_getguardsize(&attributes, &guard));
  stack_setup_check(pthread_attr_destroy(&attributes));
  stack_setup_check(getrlimit(RLIMIT_STACK, &after));
  stack_setup_check(size == 128UL * 1024 && guard >= (size_t)page_size &&
                            before.rlim_cur == after.rlim_cur &&
                            before.rlim_max == after.rlim_max
                        ? 0
                        : -1);
}
