cmake_minimum_required(VERSION 3.21)
project(install_order NONE)
include("${CPKT_REPO}/cmake/CpktDependencies.cmake")
foreach(kind static shared)
  ExternalProject_Add(${kind} SOURCE_DIR "${CMAKE_SOURCE_DIR}"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND "" CONFIGURE_COMMAND ""
    BUILD_COMMAND "${CMAKE_COMMAND}" "-DROOT=${CMAKE_BINARY_DIR}" "-DMODE=build-${kind}" -P "${CMAKE_SOURCE_DIR}/phase.cmake"
    INSTALL_COMMAND "${CMAKE_COMMAND}" "-DROOT=${CMAKE_BINARY_DIR}" "-DMODE=install-${kind}" -P "${CMAKE_SOURCE_DIR}/phase.cmake")
endforeach()
if(ENFORCE_ORDER)
  cpkt_order_shared_install(shared static)
endif()
