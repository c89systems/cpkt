function(cpkt_append_dependency_contract_file out_var path label)
  set(_contract "${${out_var}}")
  if(EXISTS "${path}")
    file(SHA256 "${path}" _sha256)
    file(RELATIVE_PATH _relpath "${CMAKE_SOURCE_DIR}" "${path}")
    string(APPEND _contract "file:${label}:${_relpath}:${_sha256}\n")
  else()
    string(APPEND _contract "missing-file:${label}:${path}\n")
  endif()
  set(${out_var} "${_contract}" PARENT_SCOPE)
endfunction()

function(cpkt_append_dependency_contract_var out_var var_name)
  set(_contract "${${out_var}}")
  if(DEFINED ${var_name})
    string(APPEND _contract "var:${var_name}=${${var_name}}\n")
  else()
    string(APPEND _contract "var:${var_name}=<UNDEFINED>\n")
  endif()
  set(${out_var} "${_contract}" PARENT_SCOPE)
endfunction()

function(cpkt_append_dependency_recipe_helper_files out_var recipe_text label)
  string(REGEX MATCHALL "cmake/[A-Za-z0-9_./-]+\\.cmake" _helper_paths
    "${recipe_text}")
  list(REMOVE_DUPLICATES _helper_paths)
  foreach(_helper_path IN LISTS _helper_paths)
    if(_helper_path STREQUAL "cmake/CpktDependencyContract.cmake"
        OR _helper_path STREQUAL "cmake/CpktDependencyArchiveCache.cmake")
      continue()
    endif()
    cpkt_append_dependency_contract_file(
      _contract "${CMAKE_SOURCE_DIR}/${_helper_path}" "${label}")
  endforeach()
  set(${out_var} "${_contract}" PARENT_SCOPE)
endfunction()

function(cpkt_append_dependency_recipe_function out_var function_name)
  set(_recipe_file "${CMAKE_SOURCE_DIR}/cmake/CpktDependencies.cmake")
  file(READ "${_recipe_file}" _recipe_text)
  string(FIND "${_recipe_text}" "function(${function_name}" _function_start)
  if(_function_start LESS 0)
    string(FIND "${_recipe_text}" "macro(${function_name}" _function_start)
  endif()
  if(_function_start LESS 0)
    message(FATAL_ERROR "dependency contract could not find recipe function: ${function_name}")
  endif()
  string(SUBSTRING "${_recipe_text}" ${_function_start} -1 _function_tail)
  string(FIND "${_function_tail}" "endfunction()" _function_end)
  set(_terminator_length 13)
  string(FIND "${_function_tail}" "endmacro()" _macro_end)
  if(_macro_end GREATER_EQUAL 0 AND (_function_end LESS 0 OR _macro_end LESS _function_end))
    set(_function_end "${_macro_end}")
    set(_terminator_length 10)
  endif()
  if(_function_end LESS 0)
    message(FATAL_ERROR "dependency contract could not find recipe function terminator: ${function_name}")
  endif()
  math(EXPR _function_length "${_function_end} + ${_terminator_length}")
  string(SUBSTRING "${_function_tail}" 0 ${_function_length} _function_text)
  string(SHA256 _function_sha256 "${_function_text}")
  set(_contract "${${out_var}}")
  string(APPEND _contract "recipe:${function_name}:${_function_sha256}\n")
  cpkt_append_dependency_recipe_helper_files(
    _contract "${_function_text}" "recipe-helper")
  set(${out_var} "${_contract}" PARENT_SCOPE)
endfunction()

function(cpkt_append_lifecycle_owned_dependency_component_root
    out_paths root root_label component)
  get_filename_component(_root_abs "${root}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
  if(root_label STREQUAL "external")
    set(_base "${CMAKE_SOURCE_DIR}/.cache/deps")
  elseif(root_label STREQUAL "build")
    set(_base "${CMAKE_SOURCE_DIR}/.cache/deps-build")
  else()
    message(FATAL_ERROR "unknown dependency component root label: ${root_label}")
  endif()
  get_filename_component(_base_abs "${_base}" ABSOLUTE)
  file(RELATIVE_PATH _base_rel "${CMAKE_SOURCE_DIR}" "${_base_abs}")
  string(REPLACE "/" ";" _base_components "${_base_rel}")
  set(_base_component_path "${CMAKE_SOURCE_DIR}")
  foreach(_base_component IN LISTS _base_components)
    set(_base_component_path "${_base_component_path}/${_base_component}")
    if(IS_SYMLINK "${_base_component_path}")
      message(FATAL_ERROR
        "lifecycle-owned ${root_label} dependency root ancestor must not be a symlink: ${_base_component_path}\n"
        "CMake will not recursively delete dependency roots below symlinked ancestors.")
    endif()
  endforeach()
  string(FIND "${_root_abs}" "${_base_abs}/" _root_under_base)
  if(_root_abs STREQUAL "${_base_abs}" OR NOT _root_under_base EQUAL 0)
    message(FATAL_ERROR
      "lifecycle-owned ${root_label} dependency component root for ${component} must be below ${_base_abs}: ${_root_abs}")
  endif()
  if(IS_SYMLINK "${_base_abs}")
    message(FATAL_ERROR "lifecycle-owned dependency root base must not be a symlink: ${_base_abs}")
  endif()
  file(RELATIVE_PATH _root_rel "${_base_abs}" "${_root_abs}")
  string(REPLACE "/" ";" _root_components "${_root_rel}")
  set(_component_path "${_base_abs}")
  foreach(_component IN LISTS _root_components)
    set(_component_path "${_component_path}/${_component}")
    if(IS_SYMLINK "${_component_path}")
      message(FATAL_ERROR
        "lifecycle-owned dependency component root must not contain symlinks: ${_component_path}")
    endif()
  endforeach()
  set(_paths "${${out_paths}}")
  list(APPEND _paths "${_root_abs}")
  set(${out_paths} "${_paths}" PARENT_SCOPE)
endfunction()

function(cpkt_prepare_dependency_component)
  cmake_parse_arguments(component ""
    "NAME;BUILD_ROOT;INSTALL_ROOT"
    "VARIABLES;DEPENDS;INPUT_FILES;RECIPE_FUNCTIONS"
    ${ARGN})
  if(NOT component_NAME OR NOT component_BUILD_ROOT OR NOT component_INSTALL_ROOT)
    message(FATAL_ERROR "cpkt_prepare_dependency_component requires NAME, BUILD_ROOT, and INSTALL_ROOT")
  endif()

  if(DEFINED CPKT_ACTIVE_COMPONENTS AND NOT component_NAME IN_LIST CPKT_ACTIVE_COMPONENTS)
    return()
  endif()
  set(_can_produce "${CPKT_BUILD_DEPENDENCIES}")
  set(_component_group "${CPKT_GROUP}")
  if(NOT _component_group)
    set(_component_group all)
  endif()
  set(_repair_preset "$ENV{CPKT_PRESET}")
  if(NOT _repair_preset)
    set(_repair_preset debug)
  endif()
  if(DEFINED CPKT_INVENTORY)
    string(JSON _component_group GET "${CPKT_INVENTORY}" components "${component_NAME}" group)
    if(NOT _component_group STREQUAL CPKT_GROUP)
      set(_can_produce OFF)
    endif()
  endif()
  if(CPKT_BORROW_ORDINARY_DEPENDENCIES)
    # Hardening imports an explicitly validated ordinary producer identity.
    file(READ "${CPKT_DEPENDENCY_CONTRACT_ROOT}/${CPKT_TARGET_ID}/${component_NAME}.txt" _ordinary)
    string(REGEX MATCH "^sha256:([a-f0-9]+)" _match "${_ordinary}")
    if(NOT _match)
      message(FATAL_ERROR "Missing ordinary component contract: ${component_NAME}")
    endif()
    set_property(GLOBAL PROPERTY "CPKT_DEPENDENCY_CONTRACT_${component_NAME}" "${CMAKE_MATCH_1}")
    return()
  endif()
  set(_contract "cpkt-dependency-component-contract-v4\n")
  foreach(_var IN ITEMS
      CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR CMAKE_CROSSCOMPILING
      CMAKE_TOOLCHAIN_FILE CMAKE_SYSROOT
      CMAKE_OSX_SYSROOT CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_C_FLAGS
      CMAKE_C_COMPILER CMAKE_C_COMPILER_ID CMAKE_C_COMPILER_VERSION
      CMAKE_CXX_FLAGS CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_ID
      CMAKE_CXX_COMPILER_VERSION CMAKE_EXE_LINKER_FLAGS
      CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS
      CMAKE_STATIC_LINKER_FLAGS CMAKE_LINKER CMAKE_AR CMAKE_RANLIB CMAKE_STRIP
      CMAKE_NM CMAKE_OBJCOPY CMAKE_OBJDUMP CMAKE_ADDR2LINE CMAKE_READELF
      CPKT_TARGET_ID CPKT_TARGET_ARCH CPKT_TARGET_OS CPKT_TARGET_LIBC
      CPKT_TOOLCHAIN_ROOT CPKT_OSXCROSS_ROOT CPKT_DEPENDENCY_BUILD_TYPE
      CPKT_TOOLCHAIN_COLLECTION_ID CPKT_MACOS_DEPLOYMENT_TARGET)
    cpkt_append_dependency_contract_var(_contract "${_var}")
  endforeach()
  # Bind the native adapter's actual cold recipe bytes too; an adapter change
  # must never leave an apparently current compiled-input receipt.
  if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/CpktVerifiedExternalProject.cmake")
    cpkt_append_dependency_contract_file(_contract
      "${CMAKE_SOURCE_DIR}/cmake/CpktVerifiedExternalProject.cmake" "external-project-adapter")
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/CpktLiteralArguments.cmake")
    cpkt_append_dependency_contract_file(_contract
      "${CMAKE_SOURCE_DIR}/cmake/CpktLiteralArguments.cmake" "literal-arguments")
  endif()
  foreach(_var IN LISTS component_VARIABLES)
    cpkt_append_dependency_contract_var(_contract "${_var}")
  endforeach()
  if(DEFINED CPKT_INVENTORY)
    string(JSON _record GET "${CPKT_INVENTORY}" components "${component_NAME}")
    string(JSON _package ERROR_VARIABLE _package_error GET "${_record}" package)
    if(NOT _package_error)
      string(JSON _record REMOVE "${_record}" package)
    endif()
    string(APPEND _contract "inventory:${_record}\n")
    string(JSON _count LENGTH "${CPKT_INVENTORY}" components "${component_NAME}" helpers)
    if(_count GREATER 0)
      math(EXPR _last "${_count} - 1")
      foreach(_index RANGE 0 ${_last})
        string(JSON _helper GET "${CPKT_INVENTORY}" components "${component_NAME}" helpers ${_index})
        cpkt_append_dependency_recipe_function(_contract "${_helper}")
      endforeach()
    endif()
  endif()
  foreach(_function IN LISTS component_RECIPE_FUNCTIONS)
    cpkt_append_dependency_recipe_function(_contract "${_function}")
  endforeach()
  foreach(_input_file IN LISTS component_INPUT_FILES)
    # Ninja/Make must re-run configuration before deciding whether the
    # component's installed artifacts are up to date. Recipe hashes alone
    # only take effect after an explicit configure.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      "${_input_file}")
    # Contract-engine and verified-archive cache changes govern bookkeeping,
    # not a component's compiled output. Hashing either invalidates every
    # component without changing a component build recipe.
    if(_input_file STREQUAL "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyContract.cmake"
        OR _input_file STREQUAL "${CMAKE_SOURCE_DIR}/cmake/CpktDependencyArchiveCache.cmake")
      continue()
    endif()
    cpkt_append_dependency_contract_file(_contract "${_input_file}" "component-input")
  endforeach()
  if(CMAKE_TOOLCHAIN_FILE)
    cpkt_append_dependency_contract_file(_contract "${CMAKE_TOOLCHAIN_FILE}" "selected-toolchain")
  endif()
  foreach(_dependency IN LISTS component_DEPENDS)
    get_property(_dependency_contract_set GLOBAL PROPERTY "CPKT_DEPENDENCY_CONTRACT_${_dependency}" SET)
    if(NOT _dependency_contract_set)
      message(FATAL_ERROR
        "dependency component ${component_NAME} was configured before its dependency contract: ${_dependency}")
    endif()
    get_property(_dependency_contract GLOBAL PROPERTY "CPKT_DEPENDENCY_CONTRACT_${_dependency}")
    string(APPEND _contract "dependency:${_dependency}:${_dependency_contract}\n")
  endforeach()

  string(SHA256 _contract_sha256 "${_contract}")
  string(PREPEND _contract "sha256:${_contract_sha256}\n")
  set(_contract_dir "${CPKT_DEPENDENCY_CONTRACT_ROOT}/${CPKT_TARGET_ID}")
  set(_contract_file "${_contract_dir}/${component_NAME}.txt")
  if(EXISTS "${_contract_file}")
    file(READ "${_contract_file}" _existing_contract)
  else()
    set(_existing_contract "")
  endif()

  set(_needs_refresh OFF)
  if(NOT _existing_contract STREQUAL "${_contract}")
    set(_needs_refresh ON)
  elseif(_can_produce AND CPKT_DEPENDENCY_PRODUCER)
    # Content verification detects missing/corrupt installs even when native
    # ExternalProject timestamps have not changed. CMake performs the repair.
    execute_process(COMMAND "${CPKT_HOST_PYTHON_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/cpkt_receipt_cli.py"
      --root "${CMAKE_SOURCE_DIR}" --group "${_component_group}" --target "${CPKT_TARGET_ID}"
      --component "${component_NAME}" --preset "${_repair_preset}"
      RESULT_VARIABLE _output_status OUTPUT_QUIET ERROR_VARIABLE _output_reason)
    if(NOT _output_status EQUAL 0)
      set(_needs_refresh ON)
    endif()
  endif()
  if(_needs_refresh)
    if(_can_produce)
      file(GLOB _readiness "${CMAKE_SOURCE_DIR}/build/verification/${CPKT_TARGET_ID}/${_component_group}/*-development.json")
      if(_readiness)
        file(REMOVE ${_readiness})
      endif()
    endif()
    if(NOT _can_produce)
      message(FATAL_ERROR
        "dependency component ${component_NAME} for ${CPKT_TARGET_ID}: build inputs changed or absent.\n"
        "Repair: make test GROUP=${_component_group} PRESET=${_repair_preset}")
    endif()
    if(NOT CPKT_EXTERNAL_ROOT_LIFECYCLE_OWNED OR NOT CPKT_DEPENDENCY_BUILD_ROOT_LIFECYCLE_OWNED)
      message(FATAL_ERROR "Changed dependency ${component_NAME} has caller-owned roots; refresh explicitly")
    endif()
    set(_roots_to_refresh "")
    cpkt_append_lifecycle_owned_dependency_component_root(
      _roots_to_refresh "${component_BUILD_ROOT}" build "${component_NAME}")
    cpkt_append_lifecycle_owned_dependency_component_root(
      _roots_to_refresh "${component_INSTALL_ROOT}" external "${component_NAME}")
    foreach(_root IN LISTS _roots_to_refresh)
      file(REMOVE_RECURSE "${_root}")
    endforeach()
    file(REMOVE "${CMAKE_SOURCE_DIR}/build/verification/${CPKT_TARGET_ID}/${_component_group}/component-${component_NAME}.json")
    file(MAKE_DIRECTORY "${_contract_dir}")
    file(WRITE "${_contract_file}" "${_contract}")
    message(STATUS "Refreshed dependency ${component_NAME}: inputs changed or verified outputs are missing/corrupt")
  endif()
  # A warm graph must validate receipts without replaying install commands when
  # native launcher/stamp scripts change. A refresh always uses the full recipe.
  set(_verified OFF)
  if(NOT _needs_refresh AND _can_produce AND CPKT_DEPENDENCY_PRODUCER)
    set(_verified ON)
  endif()
  set_property(GLOBAL PROPERTY "CPKT_VERIFIED_COMPONENT_${component_NAME}" "${_verified}")
  set_property(GLOBAL PROPERTY "CPKT_DEPENDENCY_CONTRACT_${component_NAME}" "${_contract_sha256}")
endfunction()
