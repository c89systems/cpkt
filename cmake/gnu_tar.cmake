function(cpkt_find_gnu_tar out_var)
  set(_cpkt_gnu_tar_candidates)
  if(DEFINED CPKT_GNU_TAR AND NOT CPKT_GNU_TAR STREQUAL "")
    list(APPEND _cpkt_gnu_tar_candidates "${CPKT_GNU_TAR}")
  else()
    foreach(_cpkt_gnu_tar_name gtar tar)
      unset(_cpkt_gnu_tar_candidate)
      unset(_cpkt_gnu_tar_candidate CACHE)
      find_program(_cpkt_gnu_tar_candidate NAMES "${_cpkt_gnu_tar_name}")
      if(_cpkt_gnu_tar_candidate)
        list(APPEND _cpkt_gnu_tar_candidates "${_cpkt_gnu_tar_candidate}")
      endif()
    endforeach()
  endif()

  foreach(_cpkt_gnu_tar_candidate IN LISTS _cpkt_gnu_tar_candidates)
    execute_process(
      COMMAND "${_cpkt_gnu_tar_candidate}" --version
      RESULT_VARIABLE _cpkt_gnu_tar_version_result
      OUTPUT_VARIABLE _cpkt_gnu_tar_version_output
      ERROR_VARIABLE _cpkt_gnu_tar_version_error
    )
    if(_cpkt_gnu_tar_version_result EQUAL 0)
      set(_cpkt_gnu_tar_version_text "${_cpkt_gnu_tar_version_output}${_cpkt_gnu_tar_version_error}")
      if(_cpkt_gnu_tar_version_text MATCHES "GNU tar")
        set(${out_var} "${_cpkt_gnu_tar_candidate}" PARENT_SCOPE)
        return()
      endif()
    endif()
  endforeach()

  message(FATAL_ERROR
    "GNU tar is required for deterministic release archives; install gtar or set CPKT_GNU_TAR")
endfunction()
