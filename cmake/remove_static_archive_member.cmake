foreach(_required
    CPKT_STATIC_ARCHIVE
    CPKT_STATIC_ARCHIVE_MEMBER
    CPKT_STATIC_ARCHIVER
    CPKT_STATIC_RANLIB)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

foreach(_required_path
    "${CPKT_STATIC_ARCHIVE}"
    "${CPKT_STATIC_ARCHIVER}"
    "${CPKT_STATIC_RANLIB}")
  if(NOT EXISTS "${_required_path}")
    message(FATAL_ERROR "Static archive repair input is missing: ${_required_path}")
  endif()
endforeach()

execute_process(
  COMMAND "${CPKT_STATIC_ARCHIVER}" t "${CPKT_STATIC_ARCHIVE}"
  RESULT_VARIABLE _list_result
  OUTPUT_VARIABLE _member_list
  ERROR_VARIABLE _list_error)
if(NOT _list_result EQUAL 0)
  message(FATAL_ERROR "failed to list ${CPKT_STATIC_ARCHIVE}: ${_list_error}")
endif()

string(REPLACE "\r\n" "\n" _member_list "${_member_list}")
string(REPLACE "\n" ";" _members "${_member_list}")
set(_object_members "${_members}")
list(FILTER _object_members INCLUDE REGEX "[.]o$")
if(NOT _object_members)
  message(FATAL_ERROR "static archive has no object members: ${CPKT_STATIC_ARCHIVE}")
endif()
list(FIND _members "${CPKT_STATIC_ARCHIVE_MEMBER}" _member_index)
if(NOT _member_index EQUAL -1)
  execute_process(
    COMMAND "${CPKT_STATIC_ARCHIVER}" d "${CPKT_STATIC_ARCHIVE}" "${CPKT_STATIC_ARCHIVE_MEMBER}"
    RESULT_VARIABLE _delete_result
    ERROR_VARIABLE _delete_error)
  if(NOT _delete_result EQUAL 0)
    message(FATAL_ERROR
      "failed to remove ${CPKT_STATIC_ARCHIVE_MEMBER} from ${CPKT_STATIC_ARCHIVE}: ${_delete_error}")
  endif()

  execute_process(
    COMMAND "${CPKT_STATIC_RANLIB}" "${CPKT_STATIC_ARCHIVE}"
    RESULT_VARIABLE _ranlib_result
    ERROR_VARIABLE _ranlib_error)
  if(NOT _ranlib_result EQUAL 0)
    message(FATAL_ERROR "failed to rebuild ${CPKT_STATIC_ARCHIVE} index: ${_ranlib_error}")
  endif()
endif()

execute_process(
  COMMAND "${CPKT_STATIC_ARCHIVER}" t "${CPKT_STATIC_ARCHIVE}"
  RESULT_VARIABLE _verify_result
  OUTPUT_VARIABLE _verified_member_list
  ERROR_VARIABLE _verify_error)
if(NOT _verify_result EQUAL 0)
  message(FATAL_ERROR "failed to verify ${CPKT_STATIC_ARCHIVE}: ${_verify_error}")
endif()
string(REPLACE "\r\n" "\n" _verified_member_list "${_verified_member_list}")
string(REPLACE "\n" ";" _verified_members "${_verified_member_list}")
list(FIND _verified_members "${CPKT_STATIC_ARCHIVE_MEMBER}" _verified_member_index)
if(NOT _verified_member_index EQUAL -1)
  message(FATAL_ERROR
    "nested member ${CPKT_STATIC_ARCHIVE_MEMBER} remains in ${CPKT_STATIC_ARCHIVE}")
endif()
foreach(_object_member IN LISTS _object_members)
  list(FIND _verified_members "${_object_member}" _object_index)
  if(_object_index EQUAL -1)
    message(FATAL_ERROR "archive cleanup lost ${_object_member} from ${CPKT_STATIC_ARCHIVE}")
  endif()
endforeach()
