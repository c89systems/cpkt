include("${CPKT_REPO}/tests/test-command.cmake")
set(work "${CPKT_SCRATCH}/krb5-trace-prototypes")
file(MAKE_DIRECTORY "${work}")
file(STRINGS "${CPKT_REPO}/cmake/patches/krb5_gss_trace_callback.patch" patch)
# Compile the actual patch additions, without configuring a second Kerberos.
set(declarations "")
set(private "")
set(definitions "")
foreach(line IN LISTS patch)
  if(line MATCHES "^\\+\\+\\+ b/(.*)")
    set(file "${CMAKE_MATCH_1}")
  elseif(line MATCHES "^@@")
    set(hunk "")
  elseif(line MATCHES "^\\+(.+)")
    set(added "${CMAKE_MATCH_1}\n")
    if(file MATCHES "/krb5/gssapi_krb5.h$")
      string(APPEND declarations "${added}")
    elseif(file MATCHES "/generic/gssapiP_trace.h$")
      string(APPEND private "${added}")
    elseif(file MATCHES "/krb5/init_sec_context.c$")
      string(APPEND hunk "${added}")
      if(hunk MATCHES "krb5_gss_apply_trace\\(krb5_context context\\)")
        set(definitions "${hunk}")
      endif()
    endif()
  endif()
endforeach()
file(WRITE "${work}/krb5.h" [=[#ifndef FIXTURE_KRB5_H
#define FIXTURE_KRB5_H
#define KRB5_CALLCONV
#define GSS_DLLIMP
#define NULL ((void *)0)
typedef int krb5_error_code;
typedef struct fixture_context *krb5_context;
typedef struct { const char *message; } krb5_trace_info;
krb5_error_code krb5_set_trace_callback(krb5_context,
    void (*)(krb5_context, const krb5_trace_info *, void *), void *);
#endif
]=])
file(WRITE "${work}/gssapiP_trace.h" "${private}")
file(WRITE "${work}/trace.c" "#include \"krb5.h\"\n#include \"gssapiP_trace.h\"\n${declarations}${definitions}")
set(command "${CPKT_CC}" -std=c89 -Wall -Wextra -Werror -Wmissing-prototypes -pedantic
  -c "-I${work}" "${work}/trace.c" -o "${work}/trace.o")
cpkt_test_command(${command})
set(prototype "krb5_error_code krb5_gss_apply_trace(krb5_context context);")
string(REPLACE "${prototype}" "" missing "${private}")
file(WRITE "${work}/gssapiP_trace.h" "${missing}")
cpkt_test_failure(krb5_gss_apply_trace ${command})
string(REPLACE "${prototype}" "krb5_error_code krb5_gss_apply_trace(int context);" conflict "${private}")
file(WRITE "${work}/gssapiP_trace.h" "${conflict}")
cpkt_test_failure(krb5_gss_apply_trace ${command})
file(WRITE "${work}/gssapiP_trace.h" "${private}")
string(REPLACE "gss_krb5_set_trace_callback(gss_krb5_trace_callback callback, void *data);" "" missing "${declarations}")
file(WRITE "${work}/trace.c" "#include \"krb5.h\"\n#include \"gssapiP_trace.h\"\n${missing}${definitions}")
cpkt_test_failure(gss_krb5_set_trace_callback ${command})
