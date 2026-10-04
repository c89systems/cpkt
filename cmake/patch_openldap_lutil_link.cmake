if(NOT DEFINED CPKT_OPENLDAP_SOURCE_DIR)
  message(FATAL_ERROR "CPKT_OPENLDAP_SOURCE_DIR is required")
endif()

set(_cpkt_openldap_makefile
  "${CPKT_OPENLDAP_SOURCE_DIR}/libraries/libldap/Makefile.in")
if(NOT EXISTS "${_cpkt_openldap_makefile}")
  message(FATAL_ERROR "missing OpenLDAP libldap makefile: ${_cpkt_openldap_makefile}")
endif()

file(READ "${_cpkt_openldap_makefile}" _cpkt_openldap_contents)
set(_cpkt_openldap_old
  "UNIX_LINK_LIBS = $(LDAP_LIBLBER_LA) $(AC_LIBS) $(SECURITY_LIBS) $(LTHREAD_LIBS)")
set(_cpkt_openldap_new
  "UNIX_LINK_LIBS = $(LDAP_LIBLBER_LA) -Wl,$(LDAP_LIBLUTIL_A) $(AC_LIBS) $(SECURITY_LIBS) $(LTHREAD_LIBS)")
# liblutil is needed by the shared linker. Pass it as a linker argument so
# GNU libtool does not also embed the archive as a non-object static member.
# Static consumers already link the separately exported liblutil archive.
string(FIND "${_cpkt_openldap_contents}" "${_cpkt_openldap_old}" _cpkt_openldap_match)
if(_cpkt_openldap_match EQUAL -1)
  string(FIND "${_cpkt_openldap_contents}" "${_cpkt_openldap_new}"
    _cpkt_openldap_patched_match)
  if(_cpkt_openldap_patched_match EQUAL -1)
    message(FATAL_ERROR
      "OpenLDAP libldap link rule changed; cannot add liblutil safely")
  endif()
else()
  string(REPLACE "${_cpkt_openldap_old}" "${_cpkt_openldap_new}"
    _cpkt_openldap_contents "${_cpkt_openldap_contents}")
  file(WRITE "${_cpkt_openldap_makefile}" "${_cpkt_openldap_contents}")
endif()

set(_cpkt_openldap_lutil_makefile
  "${CPKT_OPENLDAP_SOURCE_DIR}/build/lib-static.mk")
if(NOT EXISTS "${_cpkt_openldap_lutil_makefile}")
  message(FATAL_ERROR "missing OpenLDAP liblutil makefile: ${_cpkt_openldap_lutil_makefile}")
endif()
file(READ "${_cpkt_openldap_lutil_makefile}" _cpkt_openldap_lutil_contents)
set(_cpkt_openldap_lutil_old "$(LIBRARY): version.o")
set(_cpkt_openldap_lutil_new "$(LIBRARY): $(OBJS) version.o")
string(FIND "${_cpkt_openldap_lutil_contents}" "${_cpkt_openldap_lutil_old}"
  _cpkt_openldap_lutil_match)
if(_cpkt_openldap_lutil_match EQUAL -1)
  string(FIND "${_cpkt_openldap_lutil_contents}" "${_cpkt_openldap_lutil_new}"
    _cpkt_openldap_lutil_patched_match)
  if(_cpkt_openldap_lutil_patched_match EQUAL -1)
    message(FATAL_ERROR
      "OpenLDAP liblutil archive rule changed; cannot add object prerequisites safely")
  endif()
else()
  string(REPLACE "${_cpkt_openldap_lutil_old}" "${_cpkt_openldap_lutil_new}"
    _cpkt_openldap_lutil_contents "${_cpkt_openldap_lutil_contents}")
  file(WRITE "${_cpkt_openldap_lutil_makefile}" "${_cpkt_openldap_lutil_contents}")
endif()
