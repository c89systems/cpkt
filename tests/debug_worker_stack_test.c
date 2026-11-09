#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct observation {
  size_t expected;
  int nested;
  int guard_access;
  int failed;
};

static void guard_fault(int number) { _exit(number == SIGSEGV ? 77 : 78); }

static void *observe(void *argument) {
  struct observation *result = argument;
  pthread_attr_t attributes;
  void *base;
  size_t size, guard;
  pthread_t child;
  pid_t process;
  int status;
  struct observation nested;
  if (pthread_getattr_np(pthread_self(), &attributes) != 0) {
    result->failed = 1;
    return NULL;
  }
  status = pthread_attr_getstack(&attributes, &base, &size);
  status |= pthread_attr_getguardsize(&attributes, &guard);
  status |= pthread_attr_destroy(&attributes);
  if (status != 0 || size != result->expected || guard == 0) {
    result->failed = 1;
    return NULL;
  }
  if (result->nested) {
    memset(&nested, 0, sizeof(nested));
    nested.expected = 128UL * 1024;
    if (pthread_create(&child, NULL, observe, &nested) != 0 ||
        pthread_join(child, NULL) != 0 || nested.failed)
      result->failed = 1;
  }
  if (result->guard_access) {
    process = fork();
    if (process == 0) {
      volatile unsigned char value;
      if (signal(SIGSEGV, guard_fault) == SIG_ERR)
        _exit(78);
      value = *((volatile unsigned char *)base - 1);
      (void)value;
      _exit(79);
    }
    if (process < 0 || waitpid(process, &status, 0) != process ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 77)
      result->failed = 1;
  }
  return NULL;
}

int main(int argc, char **argv) {
  pthread_attr_t attributes;
  const pthread_attr_t *selected;
  pthread_t worker;
  struct observation result;
  int i;
#ifdef CPKT_TEST_NATIVE_SELF_EXEC
  int status;
  pid_t child;
#endif
  if (argc != 2)
    return 2;
  if (strcmp(argv[1], "defaults") != 0 && strcmp(argv[1], "child") != 0 &&
      strcmp(argv[1], "guard") != 0)
    return 2;
  for (i = 0; i < 3; ++i) {
    memset(&result, 0, sizeof(result));
    result.expected = i == 0 ? 256UL * 1024 : 128UL * 1024;
    result.nested = i == 1;
    result.guard_access = strcmp(argv[1], "guard") == 0;
    selected = NULL;
    if (i == 0) {
      if (pthread_attr_init(&attributes) != 0 ||
          pthread_attr_setstacksize(&attributes, result.expected) != 0)
        return 1;
      selected = &attributes;
    }
    if (pthread_create(&worker, selected, observe, &result) != 0 ||
        pthread_join(worker, NULL) != 0)
      return 1;
    if (selected != NULL)
      pthread_attr_destroy(&attributes);
    if (result.failed)
      return 1;
  }
#ifdef CPKT_TEST_NATIVE_SELF_EXEC
  /* Emulated targets require the runner to launch each guest executable. */
  if (strcmp(argv[1], "defaults") == 0) {
    child = fork();
    if (child == 0) {
      execl(argv[0], argv[0], "child", (char *)0);
      _exit(1);
    }
    if (child < 0 || waitpid(child, &status, 0) != child || status != 0)
      return 1;
  }
#endif
  puts("Debug worker sizes, guards and recovery: passed");
  return 0;
}
