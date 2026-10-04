#!/usr/bin/env python3
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
sys.path.insert(0,str(root/'scripts'))
from cpkt_cmake_inputs import commands
selected=[]
for name, arguments, definition in commands((root/'CMakeLists.txt').read_text()):
    if name=='cpkt_group_add_test' and arguments.split()[:2] in (['NAME','lua_api_inventory'],['NAME','lua_export_policy']):
        selected.append(definition.replace('${CMAKE_SOURCE_DIR}','${CPKT_SOURCE_ROOT}'))
if len(selected)!=2:
    raise SystemExit('real Lua inventory/export registration commands missing')
with tempfile.TemporaryDirectory(prefix='lua-symbol-registration-',dir=root/'build') as temporary:
    fixture=Path(temporary)
    (fixture/'cmake').mkdir()
    shutil.copy(root/'cmake/components.json',fixture/'cmake/components.json')
    (fixture/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.21)
project(lua_registration NONE)
include(CTest)
set(CPKT_GROUP core)
set(CPKT_CAN_RUN_TARGET_EXECUTABLES OFF)
set(CMAKE_SYSTEM_NAME Darwin)
set(CPKT_TARGET_ID arm64-apple-darwin)
set(CPKT_SOURCE_ROOT "'''+str(root)+'''")
set(CPKT_PYTHON3_EXECUTABLE "'''+sys.executable+'''")
set(CMAKE_NM nm)
set(CMAKE_C_COMPILER cc)
set(_cpkt_lua_inventory_symbol_format darwin)
set(CPKT_LUA_FACADE_HEADER "${CMAKE_SOURCE_DIR}/lua.h")
include("'''+str(root/'cmake/CpktGroups.cmake')+'''")
foreach(name cpkt::lua_static cpkt::lua_shared cpkt_lua_shared)
  add_library(${name} UNKNOWN IMPORTED)
  set_target_properties(${name} PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/lua.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}")
endforeach()
'''+ '\n'.join(selected)+'\n')
    subprocess.run(['cmake','-S',str(fixture),'-B',str(fixture/'binary')],check=True,capture_output=True)
    result=subprocess.run(['ctest','--test-dir',str(fixture/'binary'),'-N'],check=True,capture_output=True,text=True)
    for name in ('lua_api_inventory','lua_export_policy'):
        if name not in result.stdout:raise SystemExit('Darwin did not register '+name+' without a runner')
print('Non-runnable Darwin registers both real Lua symbol gates without SDK configuration')
