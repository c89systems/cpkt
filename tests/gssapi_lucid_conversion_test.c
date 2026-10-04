#include <cpkt/gssapi.h>

#include <gssapi/gssapi_krb5.h>

#include <stdint.h>
#include <string.h>

static gss_krb5_lucid_context_v1_t native_lucid;
static unsigned char key_bytes[3] = {1, 2, 3};
static int free_calls;
static int fail_export;
static int fail_free;

OM_uint32 gss_krb5_export_lucid_sec_context(OM_uint32 *minor,
                                            gss_ctx_id_t *context,
                                            OM_uint32 version,
                                            void **lucid_out) {
  if (version != 1 || context == NULL || *context == NULL)
    return GSS_S_CALL_BAD_STRUCTURE;
  *minor = 0;
  if (fail_export) {
    *lucid_out = NULL;
    return GSS_S_FAILURE;
  }
  *context = GSS_C_NO_CONTEXT;
  *lucid_out = &native_lucid;
  return GSS_S_COMPLETE;
}

OM_uint32 gss_krb5_free_lucid_sec_context(OM_uint32 *minor, void *lucid) {
  if (lucid != &native_lucid)
    return GSS_S_CALL_BAD_STRUCTURE;
  if (fail_free) {
    *minor = 47;
    return GSS_S_FAILURE;
  }
  *minor = 0;
  ++free_calls;
  return GSS_S_COMPLETE;
}

int main(void) {
  cpkt_gss_context *context = (cpkt_gss_context *)&native_lucid;
  cpkt_gss_lucid_context *lucid = NULL;
  cpkt_gss_status minor = 9;
  memset(&native_lucid, 0, sizeof(native_lucid));
  native_lucid.version = 1;
  native_lucid.initiate = 1;
  native_lucid.send_seq = ((uint64_t)0xfedcba98UL << 32) | 0x76543210UL;
  native_lucid.recv_seq = ((uint64_t)0x87654321UL << 32) | 0xabcdef09UL;
  native_lucid.protocol = 1;
  native_lucid.cfx_kd.have_acceptor_subkey = 1;
  native_lucid.cfx_kd.ctx_key.type = 18;
  native_lucid.cfx_kd.ctx_key.length = sizeof(key_bytes);
  native_lucid.cfx_kd.ctx_key.data = key_bytes;
  native_lucid.cfx_kd.acceptor_subkey = native_lucid.cfx_kd.ctx_key;
  if (cpkt_gss_krb5_export_lucid_context(&minor, &context, &lucid) !=
          CPKT_GSS_COMPLETE ||
      minor != 0 || context != NULL || lucid == NULL ||
      lucid->send_sequence.high != 0xfedcba98UL ||
      lucid->send_sequence.low != 0x76543210UL ||
      lucid->receive_sequence.high != 0x87654321UL ||
      lucid->receive_sequence.low != 0xabcdef09UL ||
      lucid->context_key.data != key_bytes ||
      lucid->acceptor_subkey.data != key_bytes ||
      lucid->have_acceptor_subkey != 1)
    return 1;
  fail_free = 1;
  if (cpkt_gss_krb5_free_lucid_context(&minor, &lucid) != CPKT_GSS_S_FAILURE ||
      minor != 47 || lucid == NULL || lucid->context_key.data != key_bytes ||
      lucid->send_sequence.high != 0xfedcba98UL || free_calls != 0)
    return 6;
  fail_free = 0;
  if (cpkt_gss_krb5_free_lucid_context(&minor, &lucid) != CPKT_GSS_COMPLETE ||
      lucid != NULL || free_calls != 1)
    return 2;
  context = (cpkt_gss_context *)&native_lucid;
  native_lucid.protocol = 0;
  native_lucid.rfc1964_kd.sign_alg = 7;
  native_lucid.rfc1964_kd.seal_alg = 8;
  native_lucid.rfc1964_kd.ctx_key = native_lucid.cfx_kd.ctx_key;
  if (cpkt_gss_krb5_export_lucid_context(&minor, &context, &lucid) !=
          CPKT_GSS_COMPLETE ||
      lucid == NULL || lucid->sign_algorithm != 7 ||
      lucid->seal_algorithm != 8 || lucid->context_key.data != key_bytes)
    return 3;
  if (cpkt_gss_krb5_free_lucid_context(&minor, &lucid) != CPKT_GSS_COMPLETE ||
      free_calls != 2)
    return 4;
  context = (cpkt_gss_context *)&native_lucid;
  fail_export = 1;
  if (cpkt_gss_krb5_export_lucid_context(&minor, &context, &lucid) !=
          CPKT_GSS_S_FAILURE ||
      lucid != NULL || context == NULL || free_calls != 2)
    return 5;
  if (cpkt_gss_krb5_free_lucid_context(&minor, &lucid) != CPKT_GSS_COMPLETE ||
      minor != 0 || free_calls != 2)
    return 7;
  return 0;
}
