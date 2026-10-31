#include "zbuild.h"
#include <pthread.h>

/* Instrument the allocator of the actual patched implementation. */
#define zng_alloc_aligned cpkt_chorba_allocate
#define zng_free_aligned cpkt_chorba_release
#define crc32_chorba_118960_nondestructive cpkt_test_chorba_large
#define crc32_chorba_32768_nondestructive cpkt_test_chorba_medium
#define crc32_chorba_small_nondestructive cpkt_test_chorba_small
#define crc32_chorba_small_nondestructive_32bit cpkt_test_chorba_small32
#define crc32_chorba cpkt_test_chorba
#include "arch/generic/crc32_chorba_c.c"

static unsigned allocations;
static unsigned releases;
static int allocation_fails;
void *cpkt_chorba_allocate(unsigned size, unsigned alignment) {
  void *memory = NULL;
  ++allocations;
  if (allocation_fails || posix_memalign(&memory, alignment, size) != 0)
    return NULL;
  return memory;
}
void cpkt_chorba_release(void *memory) {
  if (memory != NULL)
    ++releases;
  free(memory);
}

#ifdef CPKT_TEST_SSE41
uint32_t cpkt_test_chorba_sse41_buffer(uint32_t crc, const uint64_t *input,
                                       size_t size);
#endif

static uint32_t reference(uint32_t crc, const uint8_t *input, size_t size) {
  size_t i;
  unsigned bit;
  for (i = 0; i < size; ++i) {
    crc ^= input[i];
    for (bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
  }
  return crc;
}

typedef uint32_t (*crc_function)(uint32_t, const uint8_t *, size_t);
static uint32_t large_crc(uint32_t crc, const uint8_t *input, size_t size) {
  return cpkt_test_chorba_large(crc, (const z_word_t *)input, size);
}
#if OPTIMAL_CMP == 64
static uint32_t medium_crc(uint32_t crc, const uint8_t *input, size_t size) {
  return cpkt_test_chorba_medium(crc, (const uint64_t *)input, size);
}
#endif
#ifdef CPKT_TEST_SSE41
static uint32_t sse41_crc(uint32_t crc, const uint8_t *input, size_t size) {
  return cpkt_test_chorba_sse41_buffer(crc, (const uint64_t *)input, size);
}
#endif

static int check_crc(crc_function function, const uint8_t *input, size_t size) {
  uint32_t expected = reference(0x12345678U, input, size);
  uint32_t actual;
  unsigned before = allocations;
  unsigned freed = releases;
  int failure = 0;
  actual = function(0x12345678U, input, size);
  failure |=
      actual != expected || allocations != before + 1 || releases != freed + 1;
  allocation_fails = 1;
  actual = function(0x12345678U, input, size);
  failure |=
      actual != expected || allocations != before + 2 || releases != freed + 1;
  allocation_fails = 0;
  return failure;
}

static void *worker(void *input) {
  int failure = check_crc(large_crc, input, 256 * 1024);
#if OPTIMAL_CMP == 64
  failure |= check_crc(medium_crc, input, 16 * 1024);
#endif
#ifdef CPKT_TEST_SSE41
  if (__builtin_cpu_supports("sse4.1"))
    failure |= check_crc(sse41_crc, input, 16 * 1024);
#endif
  return failure ? &allocation_fails : NULL;
}

int main(void) {
  uint8_t *input = malloc(256 * 1024);
  pthread_t thread;
  void *result;
  size_t i;
  if (input == NULL)
    return 1;
  for (i = 0; i < 256 * 1024; ++i)
    input[i] = (uint8_t)(i * 37 + i / 13);
  /* NULL attributes exercise the libc's default worker-thread stack. */
  if (pthread_create(&thread, NULL, worker, input) != 0)
    return 2;
  if (pthread_join(thread, &result) != 0)
    return 3;
  free(input);
  if (result != NULL)
    fprintf(stderr, "Chorba heap allocation, cleanup or OOM fallback failed\n");
  return result != NULL;
}
