# Non-shipped native executables use the collection runtime in every build type.
function(cpkt_use_local_runtime target)
  if(CPKT_NATIVE_BOOTLIN_RUNTIME)
    target_link_options(${target} PRIVATE ${CPKT_LOCAL_RUNTIME_LINK_OPTIONS})
    set_property(TARGET ${target} PROPERTY CROSSCOMPILING_EMULATOR "")
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # Optional dependency targets supply build-only search paths for a local
    # consumer's indirect @rpath dylibs. Shipped install paths stay relative.
    foreach(_cpkt_runtime_dependency IN LISTS ARGN)
      set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH
        "$<TARGET_FILE_DIR:${_cpkt_runtime_dependency}>")
    endforeach()
  endif()
endfunction()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux"
    AND CMAKE_SYSTEM_PROCESSOR STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR
    AND CPKT_TOOLCHAIN_ROOT)
  set(CPKT_NATIVE_BOOTLIN_RUNTIME ON)
  if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(_cpkt_glibc_loader ld-linux-x86-64.so.2)
    set(_cpkt_musl_loader ld-musl-x86_64.so.1)
  elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(_cpkt_glibc_loader ld-linux-aarch64.so.1)
    set(_cpkt_musl_loader ld-musl-aarch64.so.1)
  elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
    set(_cpkt_glibc_loader ld-linux-armhf.so.3)
    set(_cpkt_musl_loader ld-musl-armhf.so.1)
  else()
    message(FATAL_ERROR "Unsupported native Bootlin runtime architecture")
  endif()
  if(CPKT_TARGET_LIBC STREQUAL "musl")
    set(CPKT_RUNTIME_LOADER "${CMAKE_SYSROOT}/lib/${_cpkt_musl_loader}")
  else()
    set(CPKT_RUNTIME_LOADER "${CMAKE_SYSROOT}/lib/${_cpkt_glibc_loader}")
  endif()
  if(NOT EXISTS "${CPKT_RUNTIME_LOADER}")
    message(FATAL_ERROR "Selected Bootlin loader missing: ${CPKT_RUNTIME_LOADER}")
  endif()
  # Shared by CMake targets and local pkg-config verification links.
  set(_cpkt_runtime_paths "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/usr/lib"
    ${CPKT_LOCAL_RUNTIME_EXTRA_PATHS})
  list(JOIN _cpkt_runtime_paths ":" _cpkt_runtime_path)
  set(CPKT_LOCAL_RUNTIME_LINK_OPTIONS
    "-Wl,--dynamic-linker,${CPKT_RUNTIME_LOADER}"
    "-Wl,--disable-new-dtags"
    "-Wl,-rpath,${_cpkt_runtime_path}")
endif()
