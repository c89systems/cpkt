cmake_minimum_required(VERSION 3.21)
project(cpkt_lifecycle_tests NONE)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME lifecycle_dispatch
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../lifecycle_dispatch_test.py")
set_tests_properties(lifecycle_dispatch PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
