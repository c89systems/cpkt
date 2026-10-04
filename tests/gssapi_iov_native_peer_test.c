#include <cpkt/gssapi.h>

#include <gssapi/gssapi_ext.h>

#include <stdlib.h>
#include <string.h>

static int calls;
static int releases;

OM_uint32 gss_wrap_iov(OM_uint32 *minor, gss_ctx_id_t context, int conf,
                       gss_qop_t qop, int *conf_out, gss_iov_buffer_desc *iov,
                       int count) {
  char *header;
  (void)context;
  if (minor == NULL || conf_out == NULL || count != 2 || conf != 1 ||
      qop != 7 || iov[0].type != GSS_IOV_BUFFER_TYPE_HEADER ||
      iov[1].type != GSS_IOV_BUFFER_TYPE_DATA || iov[1].buffer.length != 4 ||
      memcmp(iov[1].buffer.value, "data", 4) != 0)
    return GSS_S_CALL_BAD_STRUCTURE;
  header = (char *)malloc(3);
  if (header == NULL)
    return GSS_S_FAILURE;
  memcpy(header, "hdr", 3);
  iov[0].type |= GSS_IOV_BUFFER_FLAG_ALLOCATED;
  iov[0].buffer.value = header;
  iov[0].buffer.length = 3;
  *conf_out = 1;
  *minor = ++calls == 1 ? 0 : 73;
  return calls == 1 ? GSS_S_COMPLETE : GSS_S_FAILURE;
}

OM_uint32 gss_release_iov_buffer(OM_uint32 *minor, gss_iov_buffer_desc *iov,
                                 int count) {
  if (count != 2 ||
      iov[0].type !=
          (GSS_IOV_BUFFER_TYPE_HEADER | GSS_IOV_BUFFER_FLAG_ALLOCATED) ||
      iov[0].buffer.length != 3 || memcmp(iov[0].buffer.value, "hdr", 3) != 0)
    return GSS_S_CALL_BAD_STRUCTURE;
  free(iov[0].buffer.value);
  iov[0].buffer.value = NULL;
  iov[0].buffer.length = 0;
  iov[0].type = GSS_IOV_BUFFER_TYPE_HEADER;
  ++releases;
  *minor = 0;
  return GSS_S_COMPLETE;
}

int main(void) {
  cpkt_gss_iov iov[2];
  cpkt_gss_status major, minor;
  int conf = 0;
  int attempt;
  for (attempt = 0; attempt != 2; ++attempt) {
    memset(iov, 0, sizeof(iov));
    iov[0].type = CPKT_GSS_IOV_HEADER;
    iov[1].type = CPKT_GSS_IOV_DATA;
    iov[1].buffer.value = (void *)"data";
    iov[1].buffer.length = 4;
    major = cpkt_gss_wrap_iov(&minor, NULL, 1, 7, &conf, iov, 2);
    if (major != (attempt == 0 ? CPKT_GSS_COMPLETE : CPKT_GSS_S_FAILURE) ||
        minor != (attempt == 0 ? 0UL : 73UL) || conf != 1 ||
        iov[0].type != (CPKT_GSS_IOV_HEADER | CPKT_GSS_IOV_ALLOCATED) ||
        iov[0].buffer.length != 3 || memcmp(iov[0].buffer.value, "hdr", 3) != 0)
      return 1;
    if (cpkt_gss_release_iov(&minor, iov, 2) != CPKT_GSS_COMPLETE ||
        iov[0].buffer.value != NULL || iov[0].buffer.length != 0 ||
        releases != attempt + 1)
      return 2;
  }
  return 0;
}
