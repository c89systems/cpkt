if(NOT DEFINED CPKT_DARWIN_LIBRARY_DIR OR CPKT_DARWIN_LIBRARY_DIR STREQUAL "")
  message(FATAL_ERROR "CPKT_DARWIN_LIBRARY_DIR is required")
endif()
if(NOT DEFINED CPKT_DARWIN_INSTALL_NAME_TOOL OR CPKT_DARWIN_INSTALL_NAME_TOOL STREQUAL "")
  message(FATAL_ERROR "CPKT_DARWIN_INSTALL_NAME_TOOL is required")
endif()
if(NOT DEFINED CPKT_DARWIN_OTOOL OR CPKT_DARWIN_OTOOL STREQUAL "")
  message(FATAL_ERROR "CPKT_DARWIN_OTOOL is required")
endif()
if(NOT DEFINED CPKT_DARWIN_STAGE_LIBRARY_DIR OR CPKT_DARWIN_STAGE_LIBRARY_DIR STREQUAL "")
  message(FATAL_ERROR "CPKT_DARWIN_STAGE_LIBRARY_DIR is required")
endif()

foreach(_required_path
    "${CPKT_DARWIN_LIBRARY_DIR}"
    "${CPKT_DARWIN_STAGE_LIBRARY_DIR}"
    "${CPKT_DARWIN_INSTALL_NAME_TOOL}"
    "${CPKT_DARWIN_OTOOL}")
  if(NOT EXISTS "${_required_path}")
    message(FATAL_ERROR "Darwin install-name normalizer input is missing: ${_required_path}")
  endif()
endforeach()

# ExternalProject uses copy_directory, which dereferences staged aliases.
# Restore them as links before changing Mach-O load commands, so every alias
# resolves to the same canonical dylib in the installed SDK.
file(GLOB_RECURSE _staged_library_names RELATIVE "${CPKT_DARWIN_STAGE_LIBRARY_DIR}"
  "${CPKT_DARWIN_STAGE_LIBRARY_DIR}/*.dylib"
  "${CPKT_DARWIN_STAGE_LIBRARY_DIR}/*.so")
foreach(_staged_name IN LISTS _staged_library_names)
  set(_staged_path "${CPKT_DARWIN_STAGE_LIBRARY_DIR}/${_staged_name}")
  if(NOT IS_SYMLINK "${_staged_path}")
    continue()
  endif()
  file(READ_SYMLINK "${_staged_path}" _link_target)
  if(IS_ABSOLUTE "${_link_target}" OR _link_target MATCHES "(^|/)\\.\\.(/|$)")
    message(FATAL_ERROR "unsafe staged Darwin library link: ${_staged_path} -> ${_link_target}")
  endif()
  set(_installed_path "${CPKT_DARWIN_LIBRARY_DIR}/${_staged_name}")
  if(NOT EXISTS "${_installed_path}")
    message(FATAL_ERROR "missing installed Darwin library alias: ${_installed_path}")
  endif()
  file(REMOVE "${_installed_path}")
  file(CREATE_LINK "${_link_target}" "${_installed_path}" SYMBOLIC)
endforeach()

# Loadable bundles may live below lib/ (for example Cyrus SASL's .so files in
# lib/sasl2). They need dependency rewrites but have no dylib install ID.
file(GLOB_RECURSE _dylib_candidates
  "${CPKT_DARWIN_LIBRARY_DIR}/*.dylib"
  "${CPKT_DARWIN_LIBRARY_DIR}/*.so")
set(_dylibs "")
set(_dylib_names "")
foreach(_candidate IN LISTS _dylib_candidates)
  get_filename_component(_candidate_name "${_candidate}" NAME)
  list(APPEND _dylib_names "${_candidate_name}")
  # Mutating a symlink alters its referent.  Normalize the real library once;
  # its conventional link names retain the same Mach-O identity.
  if(IS_SYMLINK "${_candidate}")
    continue()
  endif()
  list(APPEND _dylibs "${_candidate}")
endforeach()

if(NOT _dylibs)
  message(FATAL_ERROR "No real Darwin dylibs found in ${CPKT_DARWIN_LIBRARY_DIR}")
endif()

foreach(_dylib IN LISTS _dylibs)
  get_filename_component(_dylib_name "${_dylib}" NAME)
  execute_process(
    COMMAND "${CPKT_DARWIN_OTOOL}" -L "${_dylib}"
    RESULT_VARIABLE _otool_result
    OUTPUT_VARIABLE _otool_output
    ERROR_VARIABLE _otool_error)
  if(NOT _otool_result EQUAL 0)
    message(FATAL_ERROR "failed to inspect ${_dylib}: ${_otool_error}")
  endif()

  string(REPLACE "\r\n" "\n" _otool_output "${_otool_output}")
  string(REPLACE "\n" ";" _otool_lines "${_otool_output}")
  foreach(_otool_line IN LISTS _otool_lines)
    string(STRIP "${_otool_line}" _otool_line)
    string(REGEX MATCH "^[^ \t(]+" _load_path "${_otool_line}")
    if(NOT _load_path MATCHES "^/usr/lib/([^/]+\\.dylib)$")
      continue()
    endif()
    set(_load_name "${CMAKE_MATCH_1}")
    list(FIND _dylib_names "${_load_name}" _bundled_index)
    if(_bundled_index EQUAL -1)
      # This is a real Darwin system dependency, not another library in this
      # staged bundle, and must retain its platform-owned install name.
      continue()
    endif()
    execute_process(
      COMMAND "${CPKT_DARWIN_INSTALL_NAME_TOOL}"
        -change "${_load_path}" "@rpath/${_load_name}" "${_dylib}"
      RESULT_VARIABLE _change_result
      ERROR_VARIABLE _change_error)
    if(NOT _change_result EQUAL 0)
      message(FATAL_ERROR
        "failed to rewrite bundled Darwin dependency ${_load_path} in ${_dylib}: ${_change_error}")
    endif()
  endforeach()

  if(_dylib_name MATCHES "[.]dylib$")
    execute_process(
      COMMAND "${CPKT_DARWIN_OTOOL}" -D "${_dylib}"
      RESULT_VARIABLE _id_query_result
      OUTPUT_VARIABLE _id_query_output
      ERROR_VARIABLE _id_query_error)
    if(NOT _id_query_result EQUAL 0)
      message(FATAL_ERROR "failed to inspect Darwin install ID for ${_dylib}: ${_id_query_error}")
    endif()
    string(REPLACE "\r\n" "\n" _id_query_output "${_id_query_output}")
    string(REPLACE "\n" ";" _id_lines "${_id_query_output}")
    list(LENGTH _id_lines _id_line_count)
    set(_original_id "")
    if(_id_line_count GREATER 1)
      list(GET _id_lines 1 _original_id)
      string(STRIP "${_original_id}" _original_id)
    endif()
    if(_original_id STREQUAL "")
      execute_process(
        COMMAND "${CPKT_DARWIN_OTOOL}" -hv "${_dylib}"
        RESULT_VARIABLE _header_result
        OUTPUT_VARIABLE _header_output
        ERROR_VARIABLE _header_error)
      if(NOT _header_result EQUAL 0 OR NOT _header_output MATCHES "[ \t]BUNDLE[ \t]")
        message(FATAL_ERROR "missing Darwin install ID for non-bundle ${_dylib}: ${_header_error}")
      endif()
      continue()
    endif()
    get_filename_component(_canonical_name "${_original_id}" NAME)
    if(NOT _canonical_name MATCHES "[.]dylib$" OR
        NOT _original_id MATCHES "^(/|@rpath/|@loader_path/)")
      message(FATAL_ERROR "unexpected Darwin install ID for ${_dylib}: ${_original_id}")
    endif()
    execute_process(
      COMMAND "${CPKT_DARWIN_INSTALL_NAME_TOOL}" -id "@rpath/${_canonical_name}" "${_dylib}"
      RESULT_VARIABLE _id_result
      ERROR_VARIABLE _id_error)
    if(NOT _id_result EQUAL 0)
      message(FATAL_ERROR "failed to set Darwin install name for ${_dylib}: ${_id_error}")
    endif()
  endif()
endforeach()
