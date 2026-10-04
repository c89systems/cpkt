cmake_minimum_required(VERSION 3.21)

project(cpkt_lua_runtime_c89_example LANGUAGES C)

find_package(CpktLuaRuntime CONFIG REQUIRED)

add_executable(cpkt_lua_runtime_c89_example main.c host_module.c)
set_source_files_properties(main.c PROPERTIES
  COMPILE_OPTIONS "-std=c89;-Wall;-Wextra;-Wpedantic")
set_source_files_properties(host_module.c PROPERTIES
  COMPILE_OPTIONS "-std=c99;-Wall;-Wextra;-Wpedantic")
target_link_libraries(cpkt_lua_runtime_c89_example PRIVATE cpkt::lua_runtime)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/example_file.lua"
  "local host = require('example_host')\n"
  "local chunk = require('example_chunk')\n"
  "if host.context ~= 'example-context' then error('bad file context') end\n"
  "if chunk.value ~= 'chunk' then error('bad file chunk') end\n"
  "if package.path ~= 'zero/?.lua;first/?.lua' then error('bad package path') end\n"
  "if package.cpath ~= 'zero/?.so;first/?.so' then error('bad package cpath') end\n"
  "if example_name ~= 'facade' then error('bad string global') end\n"
  "if example_flag ~= true then error('bad boolean global') end\n"
  "if example_count ~= 7 then error('bad integer global') end\n"
  "if example_number ~= 2.5 then error('bad number global') end\n"
  "if required_side_effect ~= 'ok' then error('bad require side effect') end\n"
  "if arg[0] ~= '${CMAKE_CURRENT_BINARY_DIR}/example_file.lua' then error('bad arg0') end\n"
  "if arg[1] ~= 'one' or arg[2] ~= 'two' then error('bad argv') end\n")
