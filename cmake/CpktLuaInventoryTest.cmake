  cpkt_group_add_test(
      NAME lua_api_inventory
      COMMAND
        "${CPKT_PYTHON3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/generate_lua_api_inventory.py"
        --include-dir "$<TARGET_PROPERTY:cpkt::lua_static,INTERFACE_INCLUDE_DIRECTORIES>"
        --native-library "$<TARGET_FILE:cpkt::lua_shared>"
        --facade-library "$<TARGET_FILE:cpkt_lua_shared>"
        --facade-header "${CPKT_LUA_FACADE_HEADER}"
        --symbol-tool "${CMAKE_NM}"
        --symbol-format "${_cpkt_lua_inventory_symbol_format}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/lua-api-inventory.json")
