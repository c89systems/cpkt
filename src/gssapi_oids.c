#include <cpkt/gssapi.h>

#include <gssapi/gssapi.h>
#include <gssapi/gssapi_ext.h>
#include <gssapi/gssapi_krb5.h>

/* The staged MIT Kerberos archive defines this public OID, but its shared
 * library omits the exported variable. Keep the provider's exact 11-byte
 * descriptor available to both facade linkage modes without importing a
 * symbol that the bundled shared library cannot resolve. */
static unsigned char cpkt_gss_odbc_session_key_bytes[11] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x12, 0x01, 0x02, 0x02, 0x05, 0x05};
static gss_OID_desc cpkt_gss_odbc_session_key = {
    11, cpkt_gss_odbc_session_key_bytes};

/** C89 facade contract for cpkt_gss_oid_view; see the public header for
 * ownership and callback lifetime. */
int cpkt_gss_oid_view(const cpkt_gss_oid *oid, const void **bytes_out,
                      size_t *length_out) {
  const gss_OID_desc *native = (const gss_OID_desc *)oid;
  if (native == NULL || bytes_out == NULL || length_out == NULL)
    return 0;
  *bytes_out = native->elements;
  *length_out = native->length;
  return 1;
}

#define CPKT_GSS_OID_CASE(id, symbol)                                          \
  case CPKT_GSS_OID_##id:                                                      \
    return (const cpkt_gss_oid *)symbol

/** C89 facade contract for cpkt_gss_known_oid; see the public header for
 * ownership and callback lifetime. */
const cpkt_gss_oid *cpkt_gss_known_oid(int which) {
  switch (which) {
    CPKT_GSS_OID_CASE(HOSTBASED_SERVICE_X, GSS_C_NT_HOSTBASED_SERVICE_X);
    CPKT_GSS_OID_CASE(COMPOSITE_EXPORT_NAME, GSS_C_NT_COMPOSITE_EXPORT);
    CPKT_GSS_OID_CASE(INQ_SSPI_SESSION_KEY, GSS_C_INQ_SSPI_SESSION_KEY);
    CPKT_GSS_OID_CASE(INQ_ODBC_SESSION_KEY, &cpkt_gss_odbc_session_key);
    CPKT_GSS_OID_CASE(INQ_NEGOEX_KEY, GSS_C_INQ_NEGOEX_KEY);
    CPKT_GSS_OID_CASE(INQ_NEGOEX_VERIFY_KEY, GSS_C_INQ_NEGOEX_VERIFY_KEY);
    CPKT_GSS_OID_CASE(SEC_CONTEXT_SASL_SSF, GSS_C_SEC_CONTEXT_SASL_SSF);
    CPKT_GSS_OID_CASE(MA_MECH_CONCRETE, GSS_C_MA_MECH_CONCRETE);
    CPKT_GSS_OID_CASE(MA_MECH_PSEUDO, GSS_C_MA_MECH_PSEUDO);
    CPKT_GSS_OID_CASE(MA_MECH_COMPOSITE, GSS_C_MA_MECH_COMPOSITE);
    CPKT_GSS_OID_CASE(MA_MECH_NEGO, GSS_C_MA_MECH_NEGO);
    CPKT_GSS_OID_CASE(MA_MECH_GLUE, GSS_C_MA_MECH_GLUE);
    CPKT_GSS_OID_CASE(MA_NOT_MECH, GSS_C_MA_NOT_MECH);
    CPKT_GSS_OID_CASE(MA_DEPRECATED, GSS_C_MA_DEPRECATED);
    CPKT_GSS_OID_CASE(MA_NOT_DEFAULT_MECH, GSS_C_MA_NOT_DFLT_MECH);
    CPKT_GSS_OID_CASE(MA_INITIAL_TOKEN_FRAMED, GSS_C_MA_ITOK_FRAMED);
    CPKT_GSS_OID_CASE(MA_AUTH_INIT, GSS_C_MA_AUTH_INIT);
    CPKT_GSS_OID_CASE(MA_AUTH_TARGET, GSS_C_MA_AUTH_TARG);
    CPKT_GSS_OID_CASE(MA_AUTH_INIT_INIT, GSS_C_MA_AUTH_INIT_INIT);
    CPKT_GSS_OID_CASE(MA_AUTH_TARGET_INIT, GSS_C_MA_AUTH_TARG_INIT);
    CPKT_GSS_OID_CASE(MA_AUTH_INIT_ANON, GSS_C_MA_AUTH_INIT_ANON);
    CPKT_GSS_OID_CASE(MA_AUTH_TARGET_ANON, GSS_C_MA_AUTH_TARG_ANON);
    CPKT_GSS_OID_CASE(MA_DELEGATE_CRED, GSS_C_MA_DELEG_CRED);
    CPKT_GSS_OID_CASE(MA_INTEGRITY, GSS_C_MA_INTEG_PROT);
    CPKT_GSS_OID_CASE(MA_CONFIDENTIALITY, GSS_C_MA_CONF_PROT);
    CPKT_GSS_OID_CASE(MA_MIC, GSS_C_MA_MIC);
    CPKT_GSS_OID_CASE(MA_WRAP, GSS_C_MA_WRAP);
    CPKT_GSS_OID_CASE(MA_PROTECTION_READY, GSS_C_MA_PROT_READY);
    CPKT_GSS_OID_CASE(MA_REPLAY_DETECTION, GSS_C_MA_REPLAY_DET);
    CPKT_GSS_OID_CASE(MA_SEQUENCE_DETECTION, GSS_C_MA_OOS_DET);
    CPKT_GSS_OID_CASE(MA_CHANNEL_BINDINGS, GSS_C_MA_CBINDINGS);
    CPKT_GSS_OID_CASE(MA_FORWARD_SECRECY, GSS_C_MA_PFS);
    CPKT_GSS_OID_CASE(MA_COMPRESSION, GSS_C_MA_COMPRESS);
    CPKT_GSS_OID_CASE(MA_CONTEXT_TRANSFER, GSS_C_MA_CTX_TRANS);
    CPKT_GSS_OID_CASE(MA_NEGOEX_AND_SPNEGO, GSS_C_MA_NEGOEX_AND_SPNEGO);
    CPKT_GSS_OID_CASE(KRB5_MECH_OLD, gss_mech_krb5_old);
    CPKT_GSS_OID_CASE(KRB5_MECH_WRONG, gss_mech_krb5_wrong);
    CPKT_GSS_OID_CASE(IAKERB_MECH, gss_mech_iakerb);
    CPKT_GSS_OID_CASE(KRB5_ENTERPRISE_NAME, GSS_KRB5_NT_ENTERPRISE_NAME);
    CPKT_GSS_OID_CASE(KRB5_X509_CERT, GSS_KRB5_NT_X509_CERT);
    CPKT_GSS_OID_CASE(KRB5_NAME, gss_nt_krb5_name);
    CPKT_GSS_OID_CASE(KRB5_PRINCIPAL, gss_nt_krb5_principal);
    CPKT_GSS_OID_CASE(KRB5_CRED_NO_CI_FLAGS, GSS_KRB5_CRED_NO_CI_FLAGS_X);
    CPKT_GSS_OID_CASE(KRB5_GET_CRED_IMPERSONATOR,
                      GSS_KRB5_GET_CRED_IMPERSONATOR);
  default:
    return NULL;
  }
}

/** C89 facade contract for cpkt_gss_krb5_mechanism_set; see the public header
 * for ownership and callback lifetime. */
const cpkt_gss_oid_set *cpkt_gss_krb5_mechanism_set(int which) {
  switch (which) {
  case 0:
    return (const cpkt_gss_oid_set *)gss_mech_set_krb5;
  case 1:
    return (const cpkt_gss_oid_set *)gss_mech_set_krb5_old;
  case 2:
    return (const cpkt_gss_oid_set *)gss_mech_set_krb5_both;
  default:
    return NULL;
  }
}

/** C89 facade contract for cpkt_gss_local_login_attribute; see the public
 * header for ownership and callback lifetime. */
int cpkt_gss_local_login_attribute(const void **bytes_out, size_t *length_out) {
  if (bytes_out == NULL || length_out == NULL ||
      GSS_C_ATTR_LOCAL_LOGIN_USER == NULL)
    return 0;
  *bytes_out = GSS_C_ATTR_LOCAL_LOGIN_USER->value;
  *length_out = GSS_C_ATTR_LOCAL_LOGIN_USER->length;
  return 1;
}
