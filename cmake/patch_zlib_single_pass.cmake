if(NOT DEFINED CPKT_ZLIB_SOURCE_DIR OR CPKT_ZLIB_SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "CPKT_ZLIB_SOURCE_DIR is required")
endif()

set(cmakelists_path "${CPKT_ZLIB_SOURCE_DIR}/CMakeLists.txt")
if(NOT EXISTS "${cmakelists_path}")
  message(FATAL_ERROR "zlib CMakeLists.txt not found: ${cmakelists_path}")
endif()

file(READ "${cmakelists_path}" cmakelists_text)

# Keep the legacy configure probes usable without testing undefined macros in
# consumers that enable -Wundef. Empty consumer definitions still evaluate to 0.
foreach(zconf_name zconf.h zconf.h.in)
  set(zconf_template_path "${CPKT_ZLIB_SOURCE_DIR}/${zconf_name}")
  file(READ "${zconf_template_path}" zconf_template)
  foreach(feature UNISTD STDARG)
    set(legacy_probe "#if HAVE_${feature}_H-0     /* may be set to #if 1 by ./configure */\n#  define Z_HAVE_${feature}_H\n#endif")
    set(guarded_probe "#ifdef HAVE_${feature}_H\n${legacy_probe}\n#endif")
    string(FIND "${zconf_template}" "${guarded_probe}" guarded_position)
    if(guarded_position EQUAL -1)
      string(FIND "${zconf_template}" "${legacy_probe}" legacy_position)
      if(legacy_position EQUAL -1)
        message(FATAL_ERROR "zlib ${feature} header feature anchor is missing")
      endif()
      string(REPLACE "${legacy_probe}" "${guarded_probe}" zconf_template "${zconf_template}")
    endif()
  endforeach()
  file(WRITE "${zconf_template_path}" "${zconf_template}")
endforeach()

# CMake's feature probes are private to zlib. Its installed header must not
# redefine another project's generic HAVE_* macros (libssh2 uses empty ones).
if(NOT cmakelists_text MATCHES "set\\(Z_HAVE_UNISTD_H")
  string(REPLACE "#cmakedefine HAVE_STDARG_H 1" "#cmakedefine Z_HAVE_STDARG_H"
    cmakelists_text "${cmakelists_text}")
  string(REPLACE "#cmakedefine HAVE_UNISTD_H 1" "#cmakedefine Z_HAVE_UNISTD_H"
    cmakelists_text "${cmakelists_text}")
  set(zconf_configure [=[configure_file(${zlib_BINARY_DIR}/zconf.h.cmakein ${zlib_BINARY_DIR}/zconf.h)]=])
  set(zconf_namespaced [=[set(Z_HAVE_STDARG_H ${HAVE_STDARG_H})
set(Z_HAVE_UNISTD_H ${HAVE_UNISTD_H})
configure_file(${zlib_BINARY_DIR}/zconf.h.cmakein ${zlib_BINARY_DIR}/zconf.h)]=])
  string(FIND "${cmakelists_text}" "${zconf_configure}" zconf_position)
  if(zconf_position EQUAL -1)
    message(FATAL_ERROR "zlib configuration-header generation anchor is missing")
  endif()
  string(REPLACE "${zconf_configure}" "${zconf_namespaced}" cmakelists_text "${cmakelists_text}")
  file(WRITE "${cmakelists_path}" "${cmakelists_text}")
endif()

if(cmakelists_text MATCHES "add_library\\(zlib_object OBJECT")
  return()
endif()

set(old_block [=[if(ZLIB_BUILD_SHARED)
    add_library(
        zlib SHARED ${ZLIB_SRCS} ${ZLIB_PUBLIC_HDRS} ${ZLIB_PRIVATE_HDRS}
                    $<$<OR:$<BOOL:${WIN32}>,$<BOOL:${CYGWIN}>>:win32/zlib1.rc>)
    add_library(ZLIB::ZLIB ALIAS zlib)
    target_include_directories(
        zlib
        PUBLIC $<BUILD_INTERFACE:${zlib_BINARY_DIR}>
               $<BUILD_INTERFACE:${zlib_SOURCE_DIR}>
               $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_definitions(
        zlib
        PRIVATE ZLIB_BUILD
                $<$<BOOL:NOT:${HAVE_FSEEKO}>:NO_FSEEKO>
                $<$<BOOL:${HAVE___ATTR__VIS_HIDDEN}>:HAVE_HIDDEN>
                $<$<BOOL:${MSVC}>:_CRT_SECURE_NO_DEPRECATE>
                $<$<BOOL:${MSVC}>:_CRT_NONSTDC_NO_DEPRECATE>
        PUBLIC $<$<BOOL:${HAVE_OFF64_T}>:_LARGEFILE64_SOURCE=1>)
    set(INSTALL_VERSION ${zlib_VERSION})

    if(NOT CYGWIN)
        set_target_properties(zlib PROPERTIES SOVERSION ${zlib_VERSION_MAJOR}
                                              VERSION ${INSTALL_VERSION})
    endif(NOT CYGWIN)

    set_target_properties(
        zlib
        PROPERTIES DEFINE_SYMBOL ZLIB_DLL
                   EXPORT_NAME ZLIB
                   OUTPUT_NAME z)
    if(UNIX
        AND NOT APPLE
        AND NOT (CMAKE_SYSTEM_NAME STREQUAL AIX)
        AND NOT (CMAKE_SYSTEM_NAME STREQUAL SunOS))
        # On unix-like platforms the library is almost always called libz
        set_target_properties(
            zlib
            PROPERTIES LINK_FLAGS
                       "-Wl,--version-script,\"${zlib_SOURCE_DIR}/zlib.map\"")
    endif(
        UNIX
        AND NOT APPLE
        AND NOT (CMAKE_SYSTEM_NAME STREQUAL AIX)
        AND NOT (CMAKE_SYSTEM_NAME STREQUAL SunOS))
endif(ZLIB_BUILD_SHARED)

if(ZLIB_BUILD_STATIC)
    add_library(zlibstatic STATIC ${ZLIB_SRCS} ${ZLIB_PUBLIC_HDRS}
                                  ${ZLIB_PRIVATE_HDRS})
    add_library(ZLIB::ZLIBSTATIC ALIAS zlibstatic)
    target_include_directories(
        zlibstatic
        PUBLIC $<BUILD_INTERFACE:${zlib_BINARY_DIR}>
               $<BUILD_INTERFACE:${zlib_SOURCE_DIR}>
               $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_definitions(
        zlibstatic
        PRIVATE ZLIB_BUILD
                $<$<BOOL:NOT:${HAVE_FSEEKO}>:NO_FSEEKO>
                $<$<BOOL:${HAVE___ATTR__VIS_HIDDEN}>:HAVE_HIDDEN>
                $<$<BOOL:${MSVC}>:_CRT_SECURE_NO_DEPRECATE>
                $<$<BOOL:${MSVC}>:_CRT_NONSTDC_NO_DEPRECATE>
        PUBLIC $<$<BOOL:${HAVE_OFF64_T}>:_LARGEFILE64_SOURCE=1>)
    set_target_properties(
        zlibstatic PROPERTIES EXPORT_NAME ZLIBSTATIC OUTPUT_NAME
                                                     z${zlib_static_suffix})
endif(ZLIB_BUILD_STATIC)
]=])

set(new_block [=[if((UNIX AND NOT WIN32 AND NOT CYGWIN) AND ZLIB_BUILD_SHARED AND ZLIB_BUILD_STATIC)
    add_library(zlib_object OBJECT ${ZLIB_SRCS} ${ZLIB_PUBLIC_HDRS}
                                   ${ZLIB_PRIVATE_HDRS})
    set_target_properties(zlib_object PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(
        zlib_object
        PUBLIC $<BUILD_INTERFACE:${zlib_BINARY_DIR}>
               $<BUILD_INTERFACE:${zlib_SOURCE_DIR}>
               $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_definitions(
        zlib_object
        PRIVATE ZLIB_BUILD
                $<$<BOOL:NOT:${HAVE_FSEEKO}>:NO_FSEEKO>
                $<$<BOOL:${HAVE___ATTR__VIS_HIDDEN}>:HAVE_HIDDEN>
        PUBLIC $<$<BOOL:${HAVE_OFF64_T}>:_LARGEFILE64_SOURCE=1>)

    add_library(
        zlib SHARED $<TARGET_OBJECTS:zlib_object>
                    $<$<OR:$<BOOL:${WIN32}>,$<BOOL:${CYGWIN}>>:win32/zlib1.rc>)
    add_library(ZLIB::ZLIB ALIAS zlib)
    target_include_directories(
        zlib
        PUBLIC $<BUILD_INTERFACE:${zlib_BINARY_DIR}>
               $<BUILD_INTERFACE:${zlib_SOURCE_DIR}>
               $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    set(INSTALL_VERSION ${zlib_VERSION})

    if(NOT CYGWIN)
        set_target_properties(zlib PROPERTIES SOVERSION ${zlib_VERSION_MAJOR}
                                              VERSION ${INSTALL_VERSION})
    endif(NOT CYGWIN)

    set_target_properties(
        zlib
        PROPERTIES DEFINE_SYMBOL ZLIB_DLL
                   EXPORT_NAME ZLIB
                   OUTPUT_NAME z)
    if(UNIX
        AND NOT APPLE
        AND NOT (CMAKE_SYSTEM_NAME STREQUAL AIX)
        AND NOT (CMAKE_SYSTEM_NAME STREQUAL SunOS))
        set_target_properties(
            zlib
            PROPERTIES LINK_FLAGS
                       "-Wl,--version-script,\"${zlib_SOURCE_DIR}/zlib.map\"")
    endif(
        UNIX
        AND NOT APPLE
        AND NOT (CMAKE_SYSTEM_NAME STREQUAL AIX)
        AND NOT (CMAKE_SYSTEM_NAME STREQUAL SunOS))

    add_library(zlibstatic STATIC $<TARGET_OBJECTS:zlib_object>)
    add_library(ZLIB::ZLIBSTATIC ALIAS zlibstatic)
    target_include_directories(
        zlibstatic
        PUBLIC $<BUILD_INTERFACE:${zlib_BINARY_DIR}>
               $<BUILD_INTERFACE:${zlib_SOURCE_DIR}>
               $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    set_target_properties(
        zlibstatic PROPERTIES EXPORT_NAME ZLIBSTATIC OUTPUT_NAME
                                                     z${zlib_static_suffix})
elseif(ZLIB_BUILD_SHARED)
    add_library(
        zlib SHARED ${ZLIB_SRCS} ${ZLIB_PUBLIC_HDRS} ${ZLIB_PRIVATE_HDRS}
                    $<$<OR:$<BOOL:${WIN32}>,$<BOOL:${CYGWIN}>>:win32/zlib1.rc>)
    add_library(ZLIB::ZLIB ALIAS zlib)
    target_include_directories(
        zlib
        PUBLIC $<BUILD_INTERFACE:${zlib_BINARY_DIR}>
               $<BUILD_INTERFACE:${zlib_SOURCE_DIR}>
               $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_definitions(
        zlib
        PRIVATE ZLIB_BUILD
                $<$<BOOL:NOT:${HAVE_FSEEKO}>:NO_FSEEKO>
                $<$<BOOL:${HAVE___ATTR__VIS_HIDDEN}>:HAVE_HIDDEN>
                $<$<BOOL:${MSVC}>:_CRT_SECURE_NO_DEPRECATE>
                $<$<BOOL:${MSVC}>:_CRT_NONSTDC_NO_DEPRECATE>
        PUBLIC $<$<BOOL:${HAVE_OFF64_T}>:_LARGEFILE64_SOURCE=1>)
    set(INSTALL_VERSION ${zlib_VERSION})

    if(NOT CYGWIN)
        set_target_properties(zlib PROPERTIES SOVERSION ${zlib_VERSION_MAJOR}
                                              VERSION ${INSTALL_VERSION})
    endif(NOT CYGWIN)

    set_target_properties(
        zlib
        PROPERTIES DEFINE_SYMBOL ZLIB_DLL
                   EXPORT_NAME ZLIB
                   OUTPUT_NAME z)
    if(UNIX
        AND NOT APPLE
        AND NOT (CMAKE_SYSTEM_NAME STREQUAL AIX)
        AND NOT (CMAKE_SYSTEM_NAME STREQUAL SunOS))
        set_target_properties(
            zlib
            PROPERTIES LINK_FLAGS
                       "-Wl,--version-script,\"${zlib_SOURCE_DIR}/zlib.map\"")
    endif(
        UNIX
        AND NOT APPLE
        AND NOT (CMAKE_SYSTEM_NAME STREQUAL AIX)
        AND NOT (CMAKE_SYSTEM_NAME STREQUAL SunOS))
endif(ZLIB_BUILD_SHARED)

if(NOT ((UNIX AND NOT WIN32 AND NOT CYGWIN) AND ZLIB_BUILD_SHARED AND ZLIB_BUILD_STATIC))
    if(ZLIB_BUILD_STATIC)
        add_library(zlibstatic STATIC ${ZLIB_SRCS} ${ZLIB_PUBLIC_HDRS}
                                      ${ZLIB_PRIVATE_HDRS})
        add_library(ZLIB::ZLIBSTATIC ALIAS zlibstatic)
        target_include_directories(
            zlibstatic
            PUBLIC $<BUILD_INTERFACE:${zlib_BINARY_DIR}>
                   $<BUILD_INTERFACE:${zlib_SOURCE_DIR}>
                   $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
        target_compile_definitions(
            zlibstatic
            PRIVATE ZLIB_BUILD
                    $<$<BOOL:NOT:${HAVE_FSEEKO}>:NO_FSEEKO>
                    $<$<BOOL:${HAVE___ATTR__VIS_HIDDEN}>:HAVE_HIDDEN>
                    $<$<BOOL:${MSVC}>:_CRT_SECURE_NO_DEPRECATE>
                    $<$<BOOL:${MSVC}>:_CRT_NONSTDC_NO_DEPRECATE>
            PUBLIC $<$<BOOL:${HAVE_OFF64_T}>:_LARGEFILE64_SOURCE=1>)
        set_target_properties(
            zlibstatic PROPERTIES EXPORT_NAME ZLIBSTATIC OUTPUT_NAME
                                                         z${zlib_static_suffix})
    endif(ZLIB_BUILD_STATIC)
endif()
]=])

string(FIND "${cmakelists_text}" "${old_block}" block_pos)
if(block_pos EQUAL -1)
  message(FATAL_ERROR "failed to locate zlib shared/static target block for single-pass patch")
endif()

string(REPLACE "${old_block}" "${new_block}" cmakelists_text "${cmakelists_text}")
file(WRITE "${cmakelists_path}" "${cmakelists_text}")
