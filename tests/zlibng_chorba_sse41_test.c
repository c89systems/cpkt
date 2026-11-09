#define X86_SSE41
#define X86_SSE2
#define X86_FEATURES
#define zng_alloc_aligned cpkt_chorba_allocate
#define zng_free_aligned cpkt_chorba_release
#define crc32_chorba_sse41 cpkt_test_chorba_sse41
#include "arch/x86/chorba_sse41.c"

uint32_t cpkt_test_chorba_sse41_buffer(uint32_t crc, const uint64_t *input,
                                       size_t size) {
  return crc32_chorba_32768_nondestructive_sse41(crc, input, size);
}
