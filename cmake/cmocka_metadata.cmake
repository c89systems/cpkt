if(NOT CPKT_PREFIX OR NOT CPKT_SHARED_SUFFIX)
  message(FATAL_ERROR "cmocka metadata needs prefix and platform suffix")
endif()
# Upstream shared default is kept; the explicit static export cannot overwrite
# its generated cmocka-targets.cmake. All paths remain prefix relative.
file(WRITE "${CPKT_PREFIX}/lib/cmake/cmocka/cpkt-cmocka-variants.cmake" [=[
get_filename_component(_cpkt_cmocka_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT TARGET cpkt::cmocka_static)
  add_library(cpkt::cmocka_static STATIC IMPORTED)
  set_target_properties(cpkt::cmocka_static PROPERTIES
    IMPORTED_LOCATION "${_cpkt_cmocka_prefix}/lib/libcmocka.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_cpkt_cmocka_prefix}/include"
    INTERFACE_COMPILE_DEFINITIONS CMOCKA_STATIC)
endif()
if(NOT TARGET cpkt::cmocka_shared)
  add_library(cpkt::cmocka_shared INTERFACE IMPORTED)
  set_property(TARGET cpkt::cmocka_shared PROPERTY INTERFACE_LINK_LIBRARIES cmocka::cmocka)
endif()
]=])
file(GLOB _configs "${CPKT_PREFIX}/lib/cmake/cmocka/*config.cmake")
foreach(_config IN LISTS _configs)
  file(APPEND "${_config}" "\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/cpkt-cmocka-variants.cmake\")\n")
endforeach()
file(READ "${CPKT_PREFIX}/lib/pkgconfig/cmocka.pc" _pc)
file(WRITE "${CPKT_PREFIX}/lib/pkgconfig/cmocka-shared.pc" "${_pc}")
string(REPLACE "Cflags:" "Cflags: -DCMOCKA_STATIC" _static_pc "${_pc}")
string(REPLACE "-lcmocka" "\${libdir}/libcmocka.a" _static_pc "${_static_pc}")
file(WRITE "${CPKT_PREFIX}/lib/pkgconfig/cmocka-static.pc" "${_static_pc}")
