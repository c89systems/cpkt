# libssh2 1.11.1: the integrated AES-GCM MAC does not retain its derived key.
if(NOT DEFINED CPKT_LIBSSH2_SOURCE_DIR OR CPKT_LIBSSH2_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "CPKT_LIBSSH2_SOURCE_DIR is required")
endif()

set(source_path "${CPKT_LIBSSH2_SOURCE_DIR}/src/mac.c")
file(READ "${source_path}" source_text)
set(original [=[static int
mac_method_none_init(LIBSSH2_SESSION * session, unsigned char *key,
                     int *free_key, void **abstract)
{
    (void)session;
    (void)key;
    (void)free_key;
    (void)abstract;
    return 0;
}]=])
string(REPLACE "    (void)free_key;"
  "    /* AES-GCM does not retain the separately derived MAC key. */\n    *free_key = 1;"
  patched "${original}")
string(FIND "${source_text}" "${patched}" already_patched)
if(NOT already_patched EQUAL -1)
  return()
endif()
string(FIND "${source_text}" "${original}" position)
if(position EQUAL -1)
  message(FATAL_ERROR "libssh2 AES-GCM MAC initialization anchor is missing")
endif()
string(REPLACE "${original}" "${patched}" source_text "${source_text}")
file(WRITE "${source_path}" "${source_text}")
