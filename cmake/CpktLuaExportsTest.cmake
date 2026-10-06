  cpkt_group_add_test(
      NAME lua_export_policy
      COMMAND
        bash
        "${CMAKE_SOURCE_DIR}/tests/lua_export_policy_test.sh"
        "$<TARGET_FILE:cpkt_lua_shared>"
        "${CMAKE_SYSTEM_NAME}"
        "${CMAKE_NM}"
        "${CMAKE_SOURCE_DIR}/cmake/exports/cpkt_lua.txt"
        "${CMAKE_C_COMPILER}"
        "${CMAKE_SYSROOT}")
