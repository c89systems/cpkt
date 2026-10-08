include("${CPKT_REPO}/tests/test-command.cmake")
set(work "${CPKT_SCRATCH}/zconf-namespace")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
set(template [=[#if HAVE_UNISTD_H-0
#  define Z_HAVE_UNISTD_H
#endif
#if HAVE_STDARG_H-0
#  define Z_HAVE_STDARG_H
#endif
]=])
file(WRITE "${work}/zconf.h" "${template}")
file(WRITE "${work}/zconf.h.in" "${template}")
file(WRITE "${work}/CMakeLists.txt" [=[cmake_minimum_required(VERSION 3.21)
project(zconf_namespace NONE)
set(zlib_BINARY_DIR "${CMAKE_BINARY_DIR}")
file(WRITE "${zlib_BINARY_DIR}/zconf.h.cmakein" "")
file(APPEND "${zlib_BINARY_DIR}/zconf.h.cmakein" "#cmakedefine HAVE_STDARG_H 1\n")
file(APPEND "${zlib_BINARY_DIR}/zconf.h.cmakein" "#cmakedefine HAVE_UNISTD_H 1\n")
file(READ "${CMAKE_SOURCE_DIR}/zconf.h" zconf_template)
file(APPEND "${zlib_BINARY_DIR}/zconf.h.cmakein" "${zconf_template}")
set(HAVE_STDARG_H TRUE)
set(HAVE_UNISTD_H TRUE)
configure_file(${zlib_BINARY_DIR}/zconf.h.cmakein ${zlib_BINARY_DIR}/zconf.h)
# add_library(zlib_object OBJECT
]=])
cpkt_test_command("${CMAKE_COMMAND}" -S "${work}" -B "${work}/before")
file(WRITE "${work}/consumer.c" "#define HAVE_UNISTD_H\n#define HAVE_STDARG_H\n#include <zconf.h>\nint value(void) { return 0; }\n")
cpkt_test_failure(redefined "${CPKT_CC}" -std=c89 -Wall -Wextra -Werror
  "-I${work}/before" -c "${work}/consumer.c" -o "${work}/consumer.o")
foreach(pass RANGE 1 2)
  cpkt_test_command("${CMAKE_COMMAND}" "-DCPKT_ZLIB_SOURCE_DIR=${work}"
    -P "${CPKT_REPO}/cmake/patch_zlib_single_pass.cmake")
endforeach()
cpkt_test_command("${CMAKE_COMMAND}" -S "${work}" -B "${work}/patched")
foreach(value absent empty 0 1)
  set(definitions "")
  set(assertions "")
  if(value STREQUAL "absent")
    set(assertions "#if defined(HAVE_UNISTD_H) || defined(HAVE_STDARG_H)\n#error leaked private macros\n#endif\n")
  else()
    set(number "${value}")
    if(value STREQUAL "empty")
      set(number "")
    else()
      set(assertions "#if HAVE_UNISTD_H != ${number} || HAVE_STDARG_H != ${number}\n#error changed caller macros\n#endif\n")
    endif()
    set(definitions "#define HAVE_UNISTD_H ${number}\n#define HAVE_STDARG_H ${number}\n")
  endif()
  file(WRITE "${work}/consumer.c" "${definitions}#include <zconf.h>\n#ifndef Z_HAVE_UNISTD_H\n#error lost unistd\n#endif\n#ifndef Z_HAVE_STDARG_H\n#error lost stdarg\n#endif\n${assertions}int value(void) { return 0; }\n")
  cpkt_test_command("${CPKT_CC}" -std=c89 -Wall -Wextra -Wundef -Werror
    "-I${work}/patched" -c "${work}/consumer.c" -o "${work}/consumer.o")
endforeach()
