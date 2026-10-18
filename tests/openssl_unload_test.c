/* Build as a private static-OpenSSL plugin and as its OpenSSL-free host. */
#include <string.h>

#ifdef CPKT_OPENSSL_UNLOAD_PLUGIN
#include <errno.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <pthread.h>

extern int __real_pthread_key_create(pthread_key_t *, void (*)(void *));
static unsigned int key_calls;
static unsigned int fail_key;

int __wrap_pthread_key_create(pthread_key_t *key, void (*cleanup)(void *)) {
  if (++key_calls == fail_key)
    return EAGAIN;
  return __real_pthread_key_create(key, cleanup);
}

__attribute__((visibility("default"))) int
cpkt_openssl_unload_probe(const char *mode) {
  unsigned char bytes[16];
  unsigned int i;
  if (strncmp(mode, "key-failure-", 12) == 0 && strlen(mode) == 13 &&
      mode[12] >= '1' && mode[12] <= '2') {
    int result;
    fail_key = (unsigned int)(mode[12] - '0');
    result = RAND_bytes(bytes, sizeof(bytes));
    if (result != 0 || key_calls < fail_key ||
        RAND_bytes(bytes, sizeof(bytes)) != 0 ||
        RAND_priv_bytes(bytes, sizeof(bytes)) != 0)
      return 1;
    {
      OSSL_LIB_CTX *context = OSSL_LIB_CTX_new();
      if (context != NULL) {
        OSSL_LIB_CTX_free(context);
        return 1;
      }
      if (OSSL_LIB_CTX_get0_global_default() != NULL || key_calls != fail_key)
        return 1;
    }
    OPENSSL_thread_stop();
    OPENSSL_cleanup();
    OPENSSL_cleanup();
    return 0;
  }
  if (strcmp(mode, "unused") == 0)
    return 0;
  if (strcmp(mode, "cleanup-unused") == 0) {
    OPENSSL_cleanup();
    return 0;
  }
  if (strcmp(mode, "init-only") == 0) {
    if (!OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG |
                                 OPENSSL_INIT_NO_LOAD_CRYPTO_STRINGS,
                             NULL))
      return 1;
    OPENSSL_cleanup();
    return 0;
  }
  for (i = 0; i < 3; ++i) {
    if (RAND_bytes(bytes, sizeof(bytes)) != 1)
      return 1;
    if (strcmp(mode, "thread-reuse") != 0)
      break;
    OPENSSL_thread_stop();
  }
  if (strcmp(mode, "thread-stop") == 0 || strcmp(mode, "cleanup") == 0 ||
      strcmp(mode, "double-cleanup") == 0)
    OPENSSL_thread_stop();
  if (strcmp(mode, "cleanup") == 0 || strcmp(mode, "double-cleanup") == 0)
    OPENSSL_cleanup();
  if (strcmp(mode, "double-cleanup") == 0)
    OPENSSL_cleanup();
  return 0;
}
#else
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>

struct work {
  int (*probe)(const char *);
  const char *mode;
  int result;
  pthread_barrier_t *barrier;
};

static void *run(void *opaque) {
  struct work *work = opaque;
  work->result = work->probe(work->mode);
  if (work->barrier != NULL) {
    /* Keep this thread's OpenSSL state alive across another thread's stop. */
    pthread_barrier_wait(work->barrier);
    pthread_barrier_wait(work->barrier);
    if (work->probe("thread-stop") != 0)
      work->result = 1;
  }
  return NULL;
}

static size_t available_keys(void) {
  pthread_key_t keys[2048];
  size_t count = 0;
  size_t i;
  while (count < sizeof(keys) / sizeof(keys[0]) &&
         pthread_key_create(&keys[count], NULL) == 0)
    ++count;
  for (i = 0; i < count; ++i)
    if (pthread_key_delete(keys[i]) != 0)
      return 0;
  return count;
}

int main(int argc, char **argv) {
  pthread_key_t host_key;
  int sentinel = 1;
  size_t initial;
  unsigned int i;
  int worker;
  int live_worker;
  if (argc != 3 && argc != 4)
    return 2;
  worker = argc == 4;
  live_worker = worker && strcmp(argv[3], "live-worker") == 0;
  if (worker && !live_worker && strcmp(argv[3], "worker") != 0)
    return 2;
  if (pthread_key_create(&host_key, NULL) != 0 ||
      pthread_setspecific(host_key, &sentinel) != 0)
    return 2;
  initial = available_keys();
  if (initial == 0 || initial == 2048)
    return 2; /* This bounded probe must reach the platform's actual key limit.
               */
  printf("initial: %zu keys available\n", initial);
  for (i = 0; i < 3; ++i) {
    struct work work;
    void *symbol;
    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (module == NULL) {
      fprintf(stderr, "dlopen: %s\n", dlerror());
      return 2;
    }
    symbol = dlsym(module, "cpkt_openssl_unload_probe");
    if (symbol == NULL || sizeof(work.probe) != sizeof(symbol))
      return 2;
    memcpy(&work.probe, &symbol, sizeof(work.probe));
    work.mode = live_worker ? "auto" : argv[2];
    work.result = 1;
    work.barrier = NULL;
    if (worker) {
      pthread_t thread;
      pthread_barrier_t barrier;
      int main_result = 0;
      if (live_worker) {
        if (pthread_barrier_init(&barrier, NULL, 2) != 0)
          return 2;
        work.barrier = &barrier;
      }
      if (pthread_create(&thread, NULL, run, &work) != 0)
        return 2;
      if (live_worker) {
        pthread_barrier_wait(&barrier);
        main_result = work.probe("thread-stop");
        pthread_barrier_wait(&barrier);
      }
      if (pthread_join(thread, NULL) != 0)
        return 2;
      if (live_worker && (pthread_barrier_destroy(&barrier) != 0 ||
                          main_result != 0 || work.probe("cleanup") != 0))
        return 2;
    } else {
      run(&work);
    }
    if (work.result != 0 || dlclose(module) != 0)
      return 2;
    module = dlopen(argv[1], RTLD_NOW | RTLD_NOLOAD);
    if (module != NULL) {
      dlclose(module);
      fprintf(stderr, "plugin remained loaded\n");
      return 2;
    }
    if (pthread_getspecific(host_key) != &sentinel) {
      fprintf(stderr, "OpenSSL cleanup damaged the host's TLS key\n");
      return 1;
    }
    {
      size_t after = available_keys();
      printf("unload %u: %zu keys available\n", i + 1, after);
      if (after != initial)
        return 1;
    }
  }
  return pthread_key_delete(host_key) == 0 ? 0 : 2;
}
#endif
