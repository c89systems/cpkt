#include <cpkt/gssapi.h>

#include <stdlib.h>

/* The installed utility header uses inline; keep this implementation C89. */
#define inline __inline__
#include <gssapi/gssapi_alloc.h>
#undef inline

/** Implements the public GSS allocator contract. */
void *cpkt_gss_allocation_malloc(size_t size) { return gssalloc_malloc(size); }
/** Implements the public GSS zeroed allocator contract. */
void *cpkt_gss_allocation_calloc(size_t count, size_t size) {
  return gssalloc_calloc(count, size);
}
/** Implements the public GSS reallocator contract. */
void *cpkt_gss_allocation_realloc(void *value, size_t size) {
  return gssalloc_realloc(value, size);
}
/** Implements the public GSS string allocator contract. */
char *cpkt_gss_allocation_strdup(const char *value) {
  return value == NULL ? NULL : gssalloc_strdup(value);
}
/** Implements the public GSS allocator release contract. */
void cpkt_gss_allocation_free(void *value) { gssalloc_free(value); }
