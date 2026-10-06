# This check runs before project()/toolchain provisioning, and again before
# every repository-owned executable build rule. Installed discovery is exempt.
find_program(CPKT_HOST_PYTHON_EXECUTABLE NAMES python3 REQUIRED)
execute_process(COMMAND bash
  "${CMAKE_CURRENT_LIST_DIR}/../scripts/operation.sh"
  --root "${CMAKE_SOURCE_DIR}" --group "${CPKT_GROUP}" --check
  RESULT_VARIABLE _cpkt_operation_status)
if(NOT _cpkt_operation_status EQUAL 0)
  message(FATAL_ERROR
    "Repository mutation requires verified operation delegation. Use scripts/build.sh "
    "build --group ${CPKT_GROUP} --preset debug, or scripts/operation.sh "
    "--group ${CPKT_GROUP} -- cmake <configure/build arguments>.")
endif()
if(CPKT_TARGET_ARCH AND CPKT_TARGET_OS)
  string(TOLOWER "${CPKT_TARGET_OS}" _cpkt_declared_os)
  if(_cpkt_declared_os STREQUAL "darwin")
    set(_cpkt_declared_target "${CPKT_TARGET_ARCH}-apple-darwin")
  elseif(_cpkt_declared_os STREQUAL "linux" AND CPKT_TARGET_LIBC)
    set(_cpkt_declared_target "${CPKT_TARGET_ARCH}-linux-${CPKT_TARGET_LIBC}")
  endif()
  if(_cpkt_declared_target)
    if(DEFINED ENV{CPKT_RESOLVED_TARGET} AND NOT "$ENV{CPKT_RESOLVED_TARGET}" STREQUAL ""
        AND NOT "$ENV{CPKT_RESOLVED_TARGET}" STREQUAL "${_cpkt_declared_target}")
      message(FATAL_ERROR "Conflicting declared target and CPKT_RESOLVED_TARGET")
    endif()
    set(ENV{CPKT_RESOLVED_TARGET} "${_cpkt_declared_target}")
  endif()
endif()
execute_process(COMMAND "${CPKT_HOST_PYTHON_EXECUTABLE}"
  "${CMAKE_CURRENT_LIST_DIR}/../scripts/cpkt_configure_guard.py"
  --root "${CMAKE_SOURCE_DIR}" --binary "${CMAKE_BINARY_DIR}" --group "${CPKT_GROUP}"
  --target "${CPKT_TARGET_ID}" --arch "${CPKT_TARGET_ARCH}" --os "${CPKT_TARGET_OS}"
  --libc "${CPKT_TARGET_LIBC}" --configuration "${CMAKE_BUILD_TYPE}"
  --prerequisite "${CPKT_PREREQUISITE_CONFIGURATION}" --producer "${CPKT_DEPENDENCY_PRODUCER}"
  RESULT_VARIABLE _cpkt_prerequisite_status)
if(NOT _cpkt_prerequisite_status EQUAL 0)
  message(FATAL_ERROR "Repository configure prerequisites failed before project/toolchain evaluation")
endif()
# A launcher checks delegation even when cmake --build names an individual
# target (or invokes Ninja/Make directly). Custom commands have explicit guards.
set(_cpkt_build_launcher "")
foreach(_cpkt_argument IN ITEMS bash
    "${CMAKE_SOURCE_DIR}/scripts/build-guard.sh" "${CMAKE_SOURCE_DIR}" "${CPKT_GROUP}")
  # RULE_LAUNCH_* is a shell command, not an argv list. Single quotes protect
  # spaces and substitutions; escape embedded apostrophes for POSIX shells.
  string(REPLACE "'" "'\"'\"'" _cpkt_quoted_argument "${_cpkt_argument}")
  string(APPEND _cpkt_build_launcher " '${_cpkt_quoted_argument}'")
endforeach()
string(STRIP "${_cpkt_build_launcher}" _cpkt_build_launcher)
set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${_cpkt_build_launcher}")
set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${_cpkt_build_launcher}")
set_property(GLOBAL PROPERTY RULE_LAUNCH_CUSTOM "${_cpkt_build_launcher}")

# CMake's generated clean rules do not use RULE_LAUNCH_*. Route cmake --build
# through a delegated make program so clean and --clean-first cannot mutate
# this repository without the same operation lock as ordinary build rules.
if(CMAKE_GENERATOR STREQUAL "Ninja" OR CMAKE_GENERATOR STREQUAL "Unix Makefiles")
  set(_cpkt_make_wrapper "${CMAKE_SOURCE_DIR}/scripts/native-build.sh")
  if(NOT CPKT_NATIVE_MAKE_PROGRAM)
    if(CMAKE_MAKE_PROGRAM AND NOT CMAKE_MAKE_PROGRAM STREQUAL _cpkt_make_wrapper)
      set(_cpkt_native_make_program "${CMAKE_MAKE_PROGRAM}")
    elseif(CMAKE_GENERATOR STREQUAL "Ninja")
      find_program(_cpkt_native_make_program NAMES ninja REQUIRED)
    else()
      find_program(_cpkt_native_make_program NAMES gmake make REQUIRED)
    endif()
    set(CPKT_NATIVE_MAKE_PROGRAM "${_cpkt_native_make_program}" CACHE FILEPATH
      "Native build tool behind the repository operation guard")
  endif()
  # Compiler probes run before the parent CMakeCache is written.
  set(ENV{CPKT_NATIVE_MAKE_PROGRAM} "${CPKT_NATIVE_MAKE_PROGRAM}")
  set(CMAKE_MAKE_PROGRAM "${_cpkt_make_wrapper}" CACHE FILEPATH
    "Delegated build tool for this repository" FORCE)
endif()
