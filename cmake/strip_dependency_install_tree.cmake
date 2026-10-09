if(NOT DEFINED CPKT_STRIP_ROOT OR CPKT_STRIP_ROOT STREQUAL "")
  message(FATAL_ERROR "CPKT_STRIP_ROOT is required")
endif()

if(NOT EXISTS "${CPKT_STRIP_ROOT}")
  message(FATAL_ERROR "dependency install tree does not exist: ${CPKT_STRIP_ROOT}")
endif()

if(NOT DEFINED CPKT_STRIP_BIN OR CPKT_STRIP_BIN STREQUAL "")
  message(FATAL_ERROR "CPKT_STRIP_BIN is required")
endif()

if(NOT EXISTS "${CPKT_STRIP_BIN}")
  message(FATAL_ERROR "strip tool does not exist: ${CPKT_STRIP_BIN}")
endif()

if(NOT DEFINED CPKT_STRIP_STATIC_ARCHIVES)
  set(CPKT_STRIP_STATIC_ARCHIVES ON)
endif()

if(NOT DEFINED CPKT_STRIP_SHARED_LIBRARIES)
  set(CPKT_STRIP_SHARED_LIBRARIES ON)
endif()

file(GLOB_RECURSE _cpkt_strip_candidates LIST_DIRECTORIES false
  "${CPKT_STRIP_ROOT}/lib/*.a"
  "${CPKT_STRIP_ROOT}/lib/*.dylib"
  "${CPKT_STRIP_ROOT}/lib/*.so"
  "${CPKT_STRIP_ROOT}/lib/*.so.*"
)

foreach(_candidate IN LISTS _cpkt_strip_candidates)
  if(IS_SYMLINK "${_candidate}" OR IS_DIRECTORY "${_candidate}")
    continue()
  endif()
  if(NOT CPKT_STRIP_STATIC_ARCHIVES AND _candidate MATCHES "\\.a$")
    continue()
  endif()
  if(NOT CPKT_STRIP_SHARED_LIBRARIES AND _candidate MATCHES "(\\.dylib|\\.so(\\.|$))")
    continue()
  endif()

  execute_process(
    COMMAND "${CPKT_STRIP_BIN}" -S "${_candidate}"
    RESULT_VARIABLE _strip_result
    ERROR_VARIABLE _strip_error
  )
  if(NOT _strip_result EQUAL 0)
    message(FATAL_ERROR "failed to strip dependency artifact ${_candidate}\n${_strip_error}")
  endif()
endforeach()
