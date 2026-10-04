function(cpkt_get_osxcross_lookup out_host_var out_hints_var)
  set(_osxcross_root "${CPKT_OSXCROSS_ROOT}")
  if(_osxcross_root STREQUAL "" AND DEFINED ENV{OSXCROSS_ROOT})
    set(_osxcross_root "$ENV{OSXCROSS_ROOT}")
  endif()
  if(_osxcross_root STREQUAL "" AND DEFINED ENV{HOME})
    set(_osxcross_root "$ENV{HOME}/.local/cross/osxcross")
  endif()

  set(_osxcross_host "${CPKT_OSXCROSS_HOST}")
  if(_osxcross_host STREQUAL "" AND DEFINED ENV{CPKT_OSXCROSS_HOST})
    set(_osxcross_host "$ENV{CPKT_OSXCROSS_HOST}")
  endif()
  if(_osxcross_host STREQUAL "")
    get_filename_component(_cpkt_repo_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}" DIRECTORY)
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env "OSXCROSS_ROOT=${_osxcross_root}"
        "${_cpkt_repo_root}/scripts/cpkt-toolchains.sh" discover arm64-apple-darwin
      RESULT_VARIABLE _resolver_result
      OUTPUT_VARIABLE _resolver_report
      ERROR_VARIABLE _resolver_error)
    if(NOT _resolver_result EQUAL 0 OR NOT _resolver_report MATCHES "status=ready")
      message(FATAL_ERROR "Darwin toolchain is not ready for package assertions: ${_resolver_error}${_resolver_report}")
    endif()
    string(REGEX MATCH "(^|\n)prefix=([^\n]+)" _prefix_match "${_resolver_report}")
    if(NOT _prefix_match)
      message(FATAL_ERROR "Darwin toolchain resolver did not report an osxcross prefix")
    endif()
    set(_osxcross_host "${CMAKE_MATCH_2}")
  endif()

  set(_osxcross_hints "")
  if(NOT _osxcross_root STREQUAL "")
    list(APPEND _osxcross_hints "${_osxcross_root}/bin")
  endif()
  if(DEFINED ENV{HOME})
    list(APPEND _osxcross_hints "$ENV{HOME}/.local/cross/osxcross/bin")
  endif()

  set(${out_host_var} "${_osxcross_host}" PARENT_SCOPE)
  set(${out_hints_var} "${_osxcross_hints}" PARENT_SCOPE)
endfunction()

function(cpkt_find_darwin_otool out_var)
  if(DEFINED CPKT_OTOOL AND NOT "${CPKT_OTOOL}" STREQUAL "")
    if(EXISTS "${CPKT_OTOOL}")
      set(${out_var} "${CPKT_OTOOL}" PARENT_SCOPE)
      return()
    endif()
    message(FATAL_ERROR "configured CPKT_OTOOL does not exist: ${CPKT_OTOOL}")
  endif()

  cpkt_get_osxcross_lookup(_osxcross_host _osxcross_hints)

  find_program(_cpkt_otool_bin
    NAMES "${_osxcross_host}-otool" otool
    HINTS ${_osxcross_hints})
  if(NOT _cpkt_otool_bin)
    message(FATAL_ERROR
      "otool is required to verify Darwin package artifacts; tried ${_osxcross_host}-otool and otool")
  endif()

  set(${out_var} "${_cpkt_otool_bin}" PARENT_SCOPE)
endfunction()

function(cpkt_assert_archive_contains regex description)
  if(NOT _listing MATCHES "${regex}")
    message(FATAL_ERROR "archive is missing ${description}: ${regex}")
  endif()
endfunction()

function(cpkt_assert_archive_lacks regex description)
  if(_listing MATCHES "${regex}")
    message(FATAL_ERROR "archive contains forbidden ${description}: ${regex}")
  endif()
endfunction()

function(cpkt_assert_archive_exact_matches regex expected_count description)
  string(REPLACE "\n" ";" _listing_lines "${_listing}")
  set(_actual_count 0)
  foreach(_listing_line IN LISTS _listing_lines)
    if(_listing_line MATCHES "${regex}")
      math(EXPR _actual_count "${_actual_count} + 1")
    endif()
  endforeach()
  if(NOT _actual_count EQUAL expected_count)
    message(FATAL_ERROR
      "archive must contain exactly ${expected_count} ${description}, found ${_actual_count}: ${regex}")
  endif()
endfunction()

function(cpkt_extract_archive_for_assertions out_var)
  if(NOT DEFINED CPKT_ASSERTION_WORK_ROOT OR "${CPKT_ASSERTION_WORK_ROOT}" STREQUAL "")
    message(FATAL_ERROR
      "CPKT_ASSERTION_WORK_ROOT is required; invoke package assertions through scripts/run-package-assertions.sh")
  endif()
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _extract_suffix)
  set(_extract_root
    "${CPKT_ASSERTION_WORK_ROOT}/package-assertions-${_archive_stem}-${_extract_suffix}")
  file(REMOVE_RECURSE "${_extract_root}")
  file(MAKE_DIRECTORY "${_extract_root}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${CPKT_ARCHIVE}"
    WORKING_DIRECTORY "${_extract_root}"
    RESULT_VARIABLE _extract_result
    ERROR_VARIABLE _extract_error
  )
  if(NOT _extract_result EQUAL 0)
    message(FATAL_ERROR "failed to extract ${CPKT_ARCHIVE}\n${_extract_error}")
  endif()
  set(${out_var} "${_extract_root}" PARENT_SCOPE)
endfunction()

function(cpkt_find_nm out_var)
  if(DEFINED CPKT_NM AND NOT "${CPKT_NM}" STREQUAL "")
    if(EXISTS "${CPKT_NM}")
      set(${out_var} "${CPKT_NM}" PARENT_SCOPE)
      return()
    endif()
    message(FATAL_ERROR "configured CPKT_NM does not exist: ${CPKT_NM}")
  endif()

  set(_nm_names "${CPKT_TARGET_ID}-nm" llvm-nm nm)
  set(_nm_hints "")
  if(CPKT_TARGET_ID MATCHES "darwin")
    cpkt_get_osxcross_lookup(_osxcross_host _osxcross_hints)
    set(_nm_names "${_osxcross_host}-nm" "${CPKT_TARGET_ID}-nm" llvm-nm nm)
    set(_nm_hints ${_osxcross_hints})
  endif()

  find_program(_cpkt_nm_bin
    NAMES ${_nm_names}
    HINTS ${_nm_hints})
  if(NOT _cpkt_nm_bin)
    message(FATAL_ERROR "nm is required to verify packaged static archive symbols")
  endif()

  set(${out_var} "${_cpkt_nm_bin}" PARENT_SCOPE)
endfunction()

function(cpkt_find_ar out_var)
  if(DEFINED CPKT_AR AND NOT "${CPKT_AR}" STREQUAL "")
    if(EXISTS "${CPKT_AR}")
      set(${out_var} "${CPKT_AR}" PARENT_SCOPE)
      return()
    endif()
    message(FATAL_ERROR "configured CPKT_AR does not exist: ${CPKT_AR}")
  endif()

  find_program(_cpkt_ar_bin
    NAMES "${CPKT_TARGET_ID}-ar" llvm-ar ar)
  if(NOT _cpkt_ar_bin)
    message(FATAL_ERROR "ar is required to verify packaged static archive contents")
  endif()

  set(${out_var} "${_cpkt_ar_bin}" PARENT_SCOPE)
endfunction()

function(cpkt_find_readelf out_var)
  if(DEFINED CPKT_READELF AND NOT "${CPKT_READELF}" STREQUAL "")
    if(EXISTS "${CPKT_READELF}")
      set(${out_var} "${CPKT_READELF}" PARENT_SCOPE)
      return()
    endif()
    message(FATAL_ERROR "configured CPKT_READELF does not exist: ${CPKT_READELF}")
  endif()

  find_program(_cpkt_readelf_bin
    NAMES "${CPKT_TARGET_ID}-readelf" llvm-readelf readelf)
  if(NOT _cpkt_readelf_bin)
    message(FATAL_ERROR "readelf is required to verify packaged static archive objects")
  endif()

  set(${out_var} "${_cpkt_readelf_bin}" PARENT_SCOPE)
endfunction()

function(cpkt_read_defined_symbols out_var archive_path description)
  set(_nm_args -g --defined-only)

  if(NOT EXISTS "${archive_path}")
    message(FATAL_ERROR "missing ${description}: ${archive_path}")
  endif()
  cpkt_find_nm(_cpkt_nm)
  if(CPKT_TARGET_ID MATCHES "darwin")
    set(_nm_args -gU)
  endif()
  execute_process(
    COMMAND "${_cpkt_nm}" ${_nm_args} "${archive_path}"
    RESULT_VARIABLE _nm_result
    OUTPUT_VARIABLE _nm_output
    ERROR_VARIABLE _nm_error
  )
  if(NOT _nm_result EQUAL 0)
    message(FATAL_ERROR "failed to read symbols from ${description}: ${archive_path}\n${_nm_error}")
  endif()
  set(${out_var} "${_nm_output}" PARENT_SCOPE)
endfunction()

function(cpkt_assert_static_archive_lacks_lto archive_path description)
  if(NOT EXISTS "${archive_path}")
    message(FATAL_ERROR "missing ${description}: ${archive_path}")
  endif()

  cpkt_find_ar(_cpkt_ar)
  if(CPKT_TARGET_ID MATCHES "darwin")
    cpkt_find_darwin_otool(_cpkt_otool)
  else()
    cpkt_find_readelf(_cpkt_readelf)
  endif()
  if(NOT DEFINED CPKT_ASSERTION_WORK_ROOT OR "${CPKT_ASSERTION_WORK_ROOT}" STREQUAL "")
    message(FATAL_ERROR
      "CPKT_ASSERTION_WORK_ROOT is required; invoke package assertions through scripts/run-package-assertions.sh")
  endif()
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _archive_extract_suffix)
  set(_archive_extract_dir
    "${CPKT_ASSERTION_WORK_ROOT}/package-assertions-${_archive_stem}-archive-${_archive_extract_suffix}")
  file(REMOVE_RECURSE "${_archive_extract_dir}")
  file(MAKE_DIRECTORY "${_archive_extract_dir}")
  execute_process(
    COMMAND "${_cpkt_ar}" x "${archive_path}"
    WORKING_DIRECTORY "${_archive_extract_dir}"
    RESULT_VARIABLE _ar_result
    ERROR_VARIABLE _ar_error
  )
  if(NOT _ar_result EQUAL 0)
    message(FATAL_ERROR "failed to extract ${description}: ${archive_path}\n${_ar_error}")
  endif()

  file(GLOB _archive_objects "${_archive_extract_dir}/*")
  foreach(_archive_object IN LISTS _archive_objects)
    if(IS_DIRECTORY "${_archive_object}")
      continue()
    endif()
    if(CPKT_TARGET_ID MATCHES "darwin")
      set(_section_command "${_cpkt_otool}" -l "${_archive_object}")
    else()
      set(_section_command "${_cpkt_readelf}" -S "${_archive_object}")
    endif()
    execute_process(
      COMMAND ${_section_command}
      RESULT_VARIABLE _readelf_result
      OUTPUT_VARIABLE _readelf_output
      ERROR_VARIABLE _readelf_error
    )
    if(NOT _readelf_result EQUAL 0)
      message(FATAL_ERROR
        "failed to inspect object from ${description}: ${_archive_object}\n${_readelf_error}")
    endif()
    if(_readelf_output MATCHES "\\.gnu\\.lto|__LLVM")
      message(FATAL_ERROR
        "${description} contains LTO object sections that can emit upstream diagnostics during downstream links: ${archive_path}")
    endif()
  endforeach()
  file(REMOVE_RECURSE "${_archive_extract_dir}")
endfunction()

function(cpkt_assert_darwin_install_name file_path expected_install_name description)
  cpkt_find_darwin_otool(CPKT_OTOOL_BIN)
  if(NOT CPKT_OTOOL_BIN)
    message(FATAL_ERROR "otool is required to verify ${description}")
  endif()
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "missing ${description}: ${file_path}")
  endif()

  execute_process(
    COMMAND "${CPKT_OTOOL_BIN}" -D "${file_path}"
    RESULT_VARIABLE _otool_result
    OUTPUT_VARIABLE _otool_output
    ERROR_VARIABLE _otool_error
  )
  if(NOT _otool_result EQUAL 0)
    message(FATAL_ERROR "failed to inspect ${description}: ${file_path}\n${_otool_error}")
  endif()
  string(REPLACE "\r\n" "\n" _otool_output "${_otool_output}")
  string(REPLACE "\n" ";" _otool_lines "${_otool_output}")
  set(_install_name_found OFF)
  foreach(_otool_line IN LISTS _otool_lines)
    string(STRIP "${_otool_line}" _otool_line)
    if(_otool_line STREQUAL "${expected_install_name}")
      set(_install_name_found ON)
    endif()
  endforeach()
  if(NOT _install_name_found)
    message(FATAL_ERROR "${description} must have Darwin install name [${expected_install_name}]")
  endif()
endfunction()

function(cpkt_assert_darwin_dylib_versions file_path expected_install_name
    expected_compatibility expected_current description)
  cpkt_find_darwin_otool(CPKT_OTOOL_BIN)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "missing ${description}: ${file_path}")
  endif()
  execute_process(
    COMMAND "${CPKT_OTOOL_BIN}" -L "${file_path}"
    RESULT_VARIABLE _otool_result
    OUTPUT_VARIABLE _otool_output
    ERROR_VARIABLE _otool_error)
  if(NOT _otool_result EQUAL 0)
    message(FATAL_ERROR "failed to inspect ${description}: ${file_path}\n${_otool_error}")
  endif()
  set(_expected "${expected_install_name} (compatibility version ${expected_compatibility}, current version ${expected_current})")
  string(FIND "${_otool_output}" "${_expected}" _version_position)
  if(_version_position EQUAL -1)
    message(FATAL_ERROR "${description} must advertise [${_expected}]")
  endif()
endfunction()

function(cpkt_assert_darwin_dylib_relocatable file_path description)
  cpkt_find_darwin_otool(CPKT_OTOOL_BIN)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "missing ${description}: ${file_path}")
  endif()

  if(file_path MATCHES "[.]dylib$")
    execute_process(
      COMMAND "${CPKT_OTOOL_BIN}" -hv "${file_path}"
      RESULT_VARIABLE _header_result
      OUTPUT_VARIABLE _header_output
      ERROR_VARIABLE _header_error
    )
    if(NOT _header_result EQUAL 0)
      message(FATAL_ERROR "failed to inspect Darwin Mach-O type for ${description}: ${file_path}\n${_header_error}")
    endif()
    if(NOT _header_output MATCHES "[ \t]BUNDLE[ \t]")
      execute_process(
        COMMAND "${CPKT_OTOOL_BIN}" -D "${file_path}"
        RESULT_VARIABLE _id_result
        OUTPUT_VARIABLE _id_output
        ERROR_VARIABLE _id_error
      )
      if(NOT _id_result EQUAL 0)
        message(FATAL_ERROR "failed to inspect Darwin install name for ${description}: ${file_path}\n${_id_error}")
      endif()
      string(REPLACE "\r\n" "\n" _id_output "${_id_output}")
      string(REPLACE "\n" ";" _id_lines "${_id_output}")
      set(_id_found OFF)
      foreach(_id_line IN LISTS _id_lines)
        string(STRIP "${_id_line}" _id_line)
        if(_id_line MATCHES "^@rpath/[^/]+\\.dylib$")
          set(_id_found ON)
        elseif(_id_line MATCHES "^(|.*:)$")
          continue()
        elseif(NOT _id_line STREQUAL "")
          message(FATAL_ERROR "${description} has non-rpath Darwin install name: ${_id_line}")
        endif()
      endforeach()
      if(NOT _id_found)
        message(FATAL_ERROR "${description} must have an @rpath Darwin install name")
      endif()
    endif()
  endif()

  execute_process(
    COMMAND "${CPKT_OTOOL_BIN}" -L "${file_path}"
    RESULT_VARIABLE _load_result
    OUTPUT_VARIABLE _load_output
    ERROR_VARIABLE _load_error
  )
  if(NOT _load_result EQUAL 0)
    message(FATAL_ERROR "failed to inspect Darwin load commands for ${description}: ${file_path}\n${_load_error}")
  endif()
  string(REPLACE "\r\n" "\n" _load_output "${_load_output}")
  string(REPLACE "\n" ";" _load_lines "${_load_output}")
  set(_metadata_lines "")
  foreach(_load_line IN LISTS _load_lines)
    string(STRIP "${_load_line}" _load_line)
    if(_load_line STREQUAL "" OR _load_line MATCHES ":$")
      continue()
    endif()
    list(APPEND _metadata_lines "${_load_line}")
    string(REGEX MATCH "^[^ \t(]+" _load_path "${_load_line}")
    if(_load_path MATCHES "^@rpath/[^/]+\\.dylib$")
      continue()
    endif()
    if(_load_path MATCHES "^/usr/lib/" OR _load_path MATCHES "^/System/Library/")
      continue()
    endif()
    message(FATAL_ERROR "${description} has non-relocatable Darwin dependency: ${_load_path}")
  endforeach()

  execute_process(
    COMMAND "${CPKT_OTOOL_BIN}" -l "${file_path}"
    RESULT_VARIABLE _commands_result
    OUTPUT_VARIABLE _commands_output
    ERROR_VARIABLE _commands_error
  )
  if(NOT _commands_result EQUAL 0)
    message(FATAL_ERROR "failed to inspect Darwin load-command details for ${description}: ${file_path}\n${_commands_error}")
  endif()
  string(REPLACE "\r\n" "\n" _commands_output "${_commands_output}")
  string(REPLACE "\n" ";" _command_lines "${_commands_output}")
  set(_sasl_module_parent_rpath_found OFF)
  set(_krb5_tls_module_parent_rpath_found OFF)
  foreach(_command_line IN LISTS _command_lines)
    string(STRIP "${_command_line}" _command_line)
    if(_command_line MATCHES "^path[ \t]+([^ \t]+)")
      set(_rpath "${CMAKE_MATCH_1}")
      list(APPEND _metadata_lines "${_command_line}")
      if(NOT _rpath MATCHES "^@(loader_path|executable_path)(/.*)?$")
        message(FATAL_ERROR "${description} has non-relocatable Darwin rpath: ${_rpath}")
      endif()
      if(_rpath STREQUAL "@loader_path/..")
        set(_sasl_module_parent_rpath_found ON)
      elseif(_rpath STREQUAL "@loader_path/../../..")
        set(_krb5_tls_module_parent_rpath_found ON)
      endif()
    endif()
  endforeach()
  if(file_path MATCHES "/lib/sasl2/[^/]+[.]so$" AND
      NOT _sasl_module_parent_rpath_found)
    message(FATAL_ERROR "${description} cannot resolve bundled sibling libraries from lib/sasl2")
  endif()
  if(file_path MATCHES "/lib/krb5/plugins/tls/k5tls[.]so$" AND
      NOT _krb5_tls_module_parent_rpath_found)
    message(FATAL_ERROR "${description} cannot resolve bundled sibling libraries from lib/krb5/plugins/tls")
  endif()

  set(_private_path_pattern "(/home/|/Users/|/tmp/|/var/tmp/|/usr/local/|\\.cache|deps-build|package-stage|CMakeFiles)")
  foreach(_metadata_line IN LISTS _id_lines _metadata_lines)
    string(STRIP "${_metadata_line}" _metadata_line)
    if(_metadata_line STREQUAL "" OR _metadata_line MATCHES ":$")
      continue()
    endif()
    if(_metadata_line MATCHES "${_private_path_pattern}")
      message(FATAL_ERROR "${description} contains local/private Darwin path material: ${_metadata_line}")
    endif()
  endforeach()
endfunction()

function(cpkt_assert_darwin_deployment_target file_path expected_minimum description)
  cpkt_find_darwin_otool(_otool)
  execute_process(
    COMMAND "${_otool}" -l "${file_path}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "failed to inspect Darwin deployment target for ${description}: ${_error}")
  endif()
  string(REGEX MATCHALL "cmd LC_BUILD_VERSION" _commands "${_output}")
  list(LENGTH _commands _command_count)
  string(REGEX MATCHALL "minos[ \t]+[0-9]+[.][0-9]+" _minimums "${_output}")
  list(LENGTH _minimums _minimum_count)
  if(NOT _command_count EQUAL 1 OR NOT _minimum_count EQUAL 1)
    message(FATAL_ERROR "${description} must have one LC_BUILD_VERSION minimum")
  endif()
  string(REGEX REPLACE ".*minos[ \t]+" "" _actual_minimum "${_minimums}")
  if(NOT _actual_minimum VERSION_EQUAL expected_minimum)
    message(FATAL_ERROR
      "${description} records macOS ${_actual_minimum}, expected ${expected_minimum}")
  endif()
endfunction()

function(cpkt_read_elf_dynamic_section out_var file_path description)
  cpkt_find_readelf(CPKT_READELF_BIN)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "missing ${description}: ${file_path}")
  endif()

  execute_process(
    COMMAND "${CPKT_READELF_BIN}" -d "${file_path}"
    RESULT_VARIABLE _readelf_result
    OUTPUT_VARIABLE _readelf_output
    ERROR_VARIABLE _readelf_error
  )
  if(NOT _readelf_result EQUAL 0)
    message(FATAL_ERROR "failed to inspect ${description}: ${file_path}\n${_readelf_error}")
  endif()
  set(${out_var} "${_readelf_output}" PARENT_SCOPE)
endfunction()

function(cpkt_assert_elf_runtime_metadata_output_relocatable readelf_output description)
  string(REPLACE "\r\n" "\n" _readelf_output "${readelf_output}")
  string(REPLACE "\n" ";" _readelf_lines "${_readelf_output}")
  foreach(_readelf_line IN LISTS _readelf_lines)
    if(_readelf_line MATCHES "\\((RUNPATH|RPATH)\\)[^\n]*\\[([^]]*)\\]")
      set(_runpath_list "${CMAKE_MATCH_2}")
      if(_runpath_list STREQUAL "")
        message(FATAL_ERROR "${description} has an empty RUNPATH/RPATH entry")
      endif()
      string(REPLACE ":" ";" _runpath_entries "${_runpath_list}")
      foreach(_runpath_entry IN LISTS _runpath_entries)
        if(_runpath_entry STREQUAL "")
          message(FATAL_ERROR "${description} has an empty RUNPATH/RPATH entry")
        endif()
        if(NOT _runpath_entry MATCHES "^\\$ORIGIN(/.*)?$")
          message(FATAL_ERROR
            "${description} has non-relocatable RUNPATH/RPATH entry: ${_runpath_entry}")
        endif()
      endforeach()
    endif()
  endforeach()
endfunction()

function(cpkt_assert_elf_runpath_output_relocatable readelf_output expected_runpath description)
  cpkt_assert_elf_runtime_metadata_output_relocatable(
    "${readelf_output}"
    "${description}")

  set(_expected_found OFF)
  string(REPLACE "\r\n" "\n" _readelf_output "${readelf_output}")
  string(REPLACE "\n" ";" _readelf_lines "${_readelf_output}")
  foreach(_readelf_line IN LISTS _readelf_lines)
    if(_readelf_line MATCHES "\\((RUNPATH|RPATH)\\)[^\n]*\\[([^]]*)\\]")
      string(REPLACE ":" ";" _runpath_entries "${CMAKE_MATCH_2}")
      foreach(_runpath_entry IN LISTS _runpath_entries)
        if(_runpath_entry MATCHES "^${expected_runpath}$")
          set(_expected_found ON)
        endif()
      endforeach()
    endif()
  endforeach()
  if(NOT _expected_found)
    message(FATAL_ERROR "${description} must have RUNPATH/RPATH [${expected_runpath}]")
  endif()
endfunction()

function(cpkt_assert_elf_runtime_metadata_file_relocatable file_path description)
  cpkt_read_elf_dynamic_section(_readelf_output "${file_path}" "${description}")
  cpkt_assert_elf_runtime_metadata_output_relocatable(
    "${_readelf_output}"
    "${description}")
endfunction()

function(cpkt_file_is_elf out_var file_path)
  cpkt_find_readelf(CPKT_READELF_BIN)
  execute_process(
    COMMAND "${CPKT_READELF_BIN}" -h "${file_path}"
    RESULT_VARIABLE _readelf_result
    OUTPUT_VARIABLE _readelf_output
    ERROR_QUIET)
  if(_readelf_result EQUAL 0 AND _readelf_output MATCHES "ELF Header")
    set(${out_var} ON PARENT_SCOPE)
  else()
    set(${out_var} OFF PARENT_SCOPE)
  endif()
endfunction()

function(cpkt_assert_elf_runpath_file_relocatable file_path expected_runpath description)
  cpkt_read_elf_dynamic_section(_readelf_output "${file_path}" "${description}")
  cpkt_assert_elf_runpath_output_relocatable(
    "${_readelf_output}"
    "${expected_runpath}"
    "${description}")
endfunction()

if(DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_OTOOL_LOOKUP AND CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_OTOOL_LOOKUP)
  cpkt_find_darwin_otool(_cpkt_test_otool)
  message(STATUS "CPKT_TEST_OTOOL=${_cpkt_test_otool}")
endif()
if(DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_NM_LOOKUP AND CPKT_PACKAGE_ASSERTIONS_TEST_NM_LOOKUP)
  cpkt_find_nm(_cpkt_test_nm)
  message(STATUS "CPKT_TEST_NM=${_cpkt_test_nm}")
endif()
if(DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_NM_SYMBOL_READ AND CPKT_PACKAGE_ASSERTIONS_TEST_NM_SYMBOL_READ)
  cpkt_read_defined_symbols(_cpkt_test_symbols "${CPKT_PACKAGE_ASSERTIONS_TEST_ARCHIVE}" "test archive")
  message(STATUS "CPKT_TEST_SYMBOLS=${_cpkt_test_symbols}")
endif()
if(DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_INSTALL_NAME AND CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_INSTALL_NAME)
  cpkt_assert_darwin_install_name(
    "${CPKT_PACKAGE_ASSERTIONS_TEST_DYLIB}"
    "${CPKT_PACKAGE_ASSERTIONS_TEST_EXPECTED_INSTALL_NAME}"
    "test Darwin install name")
  message(STATUS "CPKT_TEST_DARWIN_INSTALL_NAME=ok")
endif()
if(DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_VERSION AND CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_VERSION)
  cpkt_assert_darwin_dylib_versions(
    "${CPKT_PACKAGE_ASSERTIONS_TEST_DYLIB}"
    "${CPKT_PACKAGE_ASSERTIONS_TEST_EXPECTED_INSTALL_NAME}"
    "${CPKT_PACKAGE_ASSERTIONS_TEST_EXPECTED_COMPATIBILITY}"
    "${CPKT_PACKAGE_ASSERTIONS_TEST_EXPECTED_CURRENT}"
    "test Darwin dylib versions")
  message(STATUS "CPKT_TEST_DARWIN_VERSION=ok")
endif()
if(DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_RELOCATABLE AND CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_RELOCATABLE)
  cpkt_assert_darwin_dylib_relocatable(
    "${CPKT_PACKAGE_ASSERTIONS_TEST_DYLIB}"
    "test Darwin relocatable dylib")
  message(STATUS "CPKT_TEST_DARWIN_RELOCATABLE=ok")
endif()
if(DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_DEPLOYMENT AND CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_DEPLOYMENT)
  cpkt_assert_darwin_deployment_target(
    "${CPKT_PACKAGE_ASSERTIONS_TEST_DYLIB}"
    "${CPKT_PACKAGE_ASSERTIONS_TEST_EXPECTED_DEPLOYMENT}"
    "test Darwin deployment target")
  message(STATUS "CPKT_TEST_DARWIN_DEPLOYMENT=ok")
endif()
if(DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNPATH AND CPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNPATH)
  cpkt_assert_elf_runpath_file_relocatable(
    "${CPKT_PACKAGE_ASSERTIONS_TEST_ELF}"
    "${CPKT_PACKAGE_ASSERTIONS_TEST_EXPECTED_RUNPATH}"
    "test ELF runpath")
  message(STATUS "CPKT_TEST_ELF_RUNPATH=ok")
endif()
if(DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNTIME_METADATA AND CPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNTIME_METADATA)
  cpkt_assert_elf_runtime_metadata_file_relocatable(
    "${CPKT_PACKAGE_ASSERTIONS_TEST_ELF}"
    "test ELF runtime metadata")
  message(STATUS "CPKT_TEST_ELF_RUNTIME_METADATA=ok")
endif()
if((DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_OTOOL_LOOKUP AND CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_OTOOL_LOOKUP) OR
    (DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_NM_LOOKUP AND CPKT_PACKAGE_ASSERTIONS_TEST_NM_LOOKUP) OR
    (DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_NM_SYMBOL_READ AND CPKT_PACKAGE_ASSERTIONS_TEST_NM_SYMBOL_READ) OR
    (DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_INSTALL_NAME AND CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_INSTALL_NAME) OR
    (DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_VERSION AND CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_VERSION) OR
    (DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_RELOCATABLE AND CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_RELOCATABLE) OR
    (DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_DEPLOYMENT AND CPKT_PACKAGE_ASSERTIONS_TEST_DARWIN_DEPLOYMENT) OR
    (DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNPATH AND CPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNPATH) OR
    (DEFINED CPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNTIME_METADATA AND CPKT_PACKAGE_ASSERTIONS_TEST_ELF_RUNTIME_METADATA))
  return()
endif()

function(cpkt_assert_elf_runpath file_path expected_runpath description)
  cpkt_assert_elf_runpath_file_relocatable(
    "${file_path}"
    "${expected_runpath}"
    "${description}")
endfunction()

function(cpkt_assert_elf_soname file_path expected_soname description)
  cpkt_find_readelf(CPKT_READELF_BIN)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "missing ${description}: ${file_path}")
  endif()

  execute_process(
    COMMAND "${CPKT_READELF_BIN}" -d "${file_path}"
    RESULT_VARIABLE _readelf_result
    OUTPUT_VARIABLE _readelf_output
    ERROR_VARIABLE _readelf_error
  )
  if(NOT _readelf_result EQUAL 0)
    message(FATAL_ERROR "failed to inspect ${description}: ${file_path}\n${_readelf_error}")
  endif()
  if(NOT _readelf_output MATCHES "\\(SONAME\\)[^\n]*\\[${expected_soname}\\]")
    message(FATAL_ERROR "${description} must have SONAME [${expected_soname}]")
  endif()
endfunction()

function(cpkt_assert_elf_lacks_needed file_path forbidden_needed_regex description)
  cpkt_find_readelf(CPKT_READELF_BIN)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "missing ${description}: ${file_path}")
  endif()

  execute_process(
    COMMAND "${CPKT_READELF_BIN}" -d "${file_path}"
    RESULT_VARIABLE _readelf_result
    OUTPUT_VARIABLE _readelf_output
    ERROR_VARIABLE _readelf_error
  )
  if(NOT _readelf_result EQUAL 0)
    message(FATAL_ERROR "failed to inspect ${description}: ${file_path}\n${_readelf_error}")
  endif()
  if(_readelf_output MATCHES "\\(NEEDED\\)[^\n]*\\[${forbidden_needed_regex}\\]")
    message(FATAL_ERROR "${description} must not need ${forbidden_needed_regex}")
  endif()
endfunction()

function(cpkt_assert_dynamic_exports_match file_path allowed_symbol_regex description)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "missing ${description}: ${file_path}")
  endif()
  cpkt_find_nm(_cpkt_nm)
  if(CPKT_TARGET_ID MATCHES "darwin")
    set(_nm_args -gU)
  else()
    set(_nm_args -D --defined-only)
  endif()
  execute_process(
    COMMAND "${_cpkt_nm}" ${_nm_args} "${file_path}"
    RESULT_VARIABLE _nm_result
    OUTPUT_VARIABLE _nm_output
    ERROR_VARIABLE _nm_error
  )
  if(NOT _nm_result EQUAL 0)
    message(FATAL_ERROR "failed to inspect exported symbols from ${description}: ${file_path}\n${_nm_error}")
  endif()
  string(REPLACE "\n" ";" _nm_lines "${_nm_output}")
  foreach(_nm_line IN LISTS _nm_lines)
    string(STRIP "${_nm_line}" _nm_line)
    if("${_nm_line}" STREQUAL "")
      continue()
    endif()
    if(NOT _nm_line MATCHES "^[0-9A-Fa-f]+[ ]+[A-Za-z][ ]+([^ ]+)$")
      message(FATAL_ERROR "unexpected exported symbol line for ${description}: ${_nm_line}")
    endif()
    set(_symbol_name "${CMAKE_MATCH_1}")
    if(CPKT_TARGET_ID MATCHES "darwin")
      if(NOT _symbol_name MATCHES "^_")
        message(FATAL_ERROR "unexpected Mach-O export in ${description}: ${_symbol_name}")
      endif()
      string(SUBSTRING "${_symbol_name}" 1 -1 _symbol_name)
    endif()
    if(_symbol_name STREQUAL "_init" OR _symbol_name STREQUAL "_fini")
      continue()
    endif()
    if(NOT _symbol_name MATCHES "${allowed_symbol_regex}")
      message(FATAL_ERROR "${description} exports non-facade symbol: ${_symbol_name}")
    endif()
  endforeach()
endfunction()

# Compare the defined dynamic table with a target-specific, source-controlled
# allowlist. A prefix check would accept accidental cpkt_openssl_* ABI entries.
function(cpkt_assert_dynamic_exports_equal file_path allowlist_path description)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "missing ${description}: ${file_path}")
  endif()
  if(NOT EXISTS "${allowlist_path}")
    message(FATAL_ERROR "missing export allowlist for ${description}: ${allowlist_path}")
  endif()
  cpkt_find_nm(_cpkt_nm)
  if(CPKT_TARGET_ID MATCHES "darwin")
    set(_nm_args -gU "${file_path}")
  else()
    set(_nm_args -D --defined-only "${file_path}")
  endif()
  execute_process(
    COMMAND "${_cpkt_nm}" ${_nm_args}
    RESULT_VARIABLE _nm_result
    OUTPUT_VARIABLE _nm_output
    ERROR_VARIABLE _nm_error
  )
  if(NOT _nm_result EQUAL 0)
    message(FATAL_ERROR
      "failed to inspect defined dynamic exports from ${description}: ${file_path}\n${_nm_error}")
  endif()
  set(_actual_symbols "")
  string(ASCII 9 _symbol_tab)
  string(REPLACE "\n" ";" _nm_lines "${_nm_output}")
  foreach(_nm_line IN LISTS _nm_lines)
    string(STRIP "${_nm_line}" _nm_line)
    if(_nm_line STREQUAL "")
      continue()
    endif()
    string(REPLACE "${_symbol_tab}" " " _nm_line "${_nm_line}")
    string(REGEX REPLACE "^.* " "" _symbol_name "${_nm_line}")
    if(_symbol_name STREQUAL "" OR _symbol_name STREQUAL "${_nm_line}")
      message(FATAL_ERROR
        "unable to parse defined dynamic export from ${description}: ${_nm_line}")
    endif()
    if(CPKT_TARGET_ID MATCHES "darwin")
      if(NOT _symbol_name MATCHES "^_")
        message(FATAL_ERROR "unexpected Mach-O export name in ${description}: ${_symbol_name}")
      endif()
      string(SUBSTRING "${_symbol_name}" 1 -1 _symbol_name)
    endif()
    if(_symbol_name STREQUAL "_init" OR _symbol_name STREQUAL "_fini")
      continue()
    endif()
    list(APPEND _actual_symbols "${_symbol_name}")
  endforeach()
  list(REMOVE_DUPLICATES _actual_symbols)
  list(SORT _actual_symbols)

  file(STRINGS "${allowlist_path}" _expected_symbols
    REGEX "^[ \\t]*[^# \\t]")
  set(_expected_symbols_stripped "")
  foreach(_expected_symbol IN LISTS _expected_symbols)
    string(STRIP "${_expected_symbol}" _expected_symbol)
    list(APPEND _expected_symbols_stripped "${_expected_symbol}")
  endforeach()
  list(REMOVE_DUPLICATES _expected_symbols_stripped)
  list(SORT _expected_symbols_stripped)
  if(NOT "${_actual_symbols}" STREQUAL "${_expected_symbols_stripped}")
    string(REPLACE ";" "\n" _actual_display "${_actual_symbols}")
    string(REPLACE ";" "\n" _expected_display "${_expected_symbols_stripped}")
    message(FATAL_ERROR
      "${description} dynamic export allowlist mismatch\nexpected:\n${_expected_display}\nactual:\n${_actual_display}")
  endif()
endfunction()
