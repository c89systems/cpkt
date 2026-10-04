#!/usr/bin/env python3
"""Prove common-prefix installs are ordered while variant builds overlap."""
from pathlib import Path
import subprocess
import sys
import tempfile

if not __debug__:
    raise SystemExit("Install-order tests require Python assertions enabled")

repo = Path(sys.argv[1]).resolve()
build_root = Path(sys.argv[2]).resolve()
generator = sys.argv[3] if len(sys.argv) > 3 else "Ninja"
with tempfile.TemporaryDirectory(prefix="install-order-", dir=build_root) as work:
    root = Path(work)
    (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(install_order NONE)
include("${CPKT_REPO}/cmake/CpktDependencies.cmake")
foreach(kind static shared)
  ExternalProject_Add(${kind} SOURCE_DIR "${CMAKE_SOURCE_DIR}"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND "" CONFIGURE_COMMAND ""
    BUILD_COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_BINARY_DIR} -DMODE=build-${kind} -P ${CMAKE_SOURCE_DIR}/phase.cmake
    INSTALL_COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_BINARY_DIR} -DMODE=install-${kind} -P ${CMAKE_SOURCE_DIR}/phase.cmake)
endforeach()
if(ENFORCE_ORDER)
  cpkt_order_shared_install(shared static)
endif()
''')
    (root / "phase.cmake").write_text('''if(MODE STREQUAL "build-shared")
  file(APPEND "${ROOT}/events" "shared build begin\n")
  foreach(attempt RANGE 1 100)
    if(EXISTS "${ROOT}/static-active")
      file(WRITE "${ROOT}/shared-ready" "ready")
      file(APPEND "${ROOT}/events" "shared build end\n")
      return()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.05)
  endforeach()
  message(FATAL_ERROR "Shared build did not overlap the static install")
elseif(MODE STREQUAL "install-static")
  file(APPEND "${ROOT}/events" "static install begin\n")
  file(WRITE "${ROOT}/static-active" "active")
  foreach(attempt RANGE 1 100)
    if(EXISTS "${ROOT}/shared-ready")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.2)
      file(WRITE "${ROOT}/config" "static")
      file(REMOVE "${ROOT}/static-active")
      file(APPEND "${ROOT}/events" "static install end\n")
      return()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.05)
  endforeach()
  message(FATAL_ERROR "Variant builds were serialized unnecessarily")
elseif(MODE STREQUAL "install-shared")
  file(APPEND "${ROOT}/events" "shared install begin\n")
  if(EXISTS "${ROOT}/static-active")
    message(FATAL_ERROR "Concurrent common-prefix installs")
  endif()
  file(WRITE "${ROOT}/config" "shared")
  file(APPEND "${ROOT}/events" "shared install end\n")
endif()
''')
    for ordered in (False, True):
        build = root / ("ordered" if ordered else "unordered")
        subprocess.run(["cmake", "-S", str(root), "-B", str(build), "-G", generator,
                        f"-DCPKT_REPO={repo}", f"-DENFORCE_ORDER={'ON' if ordered else 'OFF'}"], check=True)
        # Two workers reproduce the race without scaling the production build.
        result = subprocess.run(["cmake", "--build", str(build), "--parallel", "2"],
                                capture_output=True, text=True)
        events = (build / "events").read_text().splitlines() if (build / "events").exists() else []
        if ordered:
            if result.returncode:
                raise SystemExit(result.stdout + result.stderr)
            expected = ["static install begin", "static install end",
                        "shared build begin", "shared build end",
                        "shared install begin", "shared install end"]
            actual = (build / "config").read_text() if (build / "config").exists() else "<missing>"
            once = len(events) == len(expected) and set(events) == set(expected)
            overlap = once and (events.index("static install begin") < events.index("shared build end")
                                < events.index("static install end"))
            ordered_installs = once and (events.index("static install end")
                                         < events.index("shared install begin")
                                         < events.index("shared install end"))
            assert once and overlap and ordered_installs and actual == "shared", (
                f"ordered install trace={events!r}, final config={actual!r}\n"
                + result.stdout + result.stderr)
        else:
            assert result.returncode and "Concurrent common-prefix installs" in result.stdout + result.stderr
print("Unordered installs conflict; ordered installs retain parallel builds and final shared metadata")
