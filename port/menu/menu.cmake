# Native retail-menu closure, linked by port/CMakeLists.txt after ntr exists.
# Overlay7 lifecycle and the actor-slot1 profile are owned by this target.
if(NINTENDO_3DS)
  set(_menu_root "${SM64DS_DECOMP_DIR}")
  set(_menu_output "${CMAKE_CURRENT_BINARY_DIR}/menu-native")
  file(STRINGS "${_menu_root}/config/arm9/overlays/ov007/symbols.txt"
       _menu_symbol_rows REGEX " kind:function")
  set(_menu_symbols)
  foreach(_row IN LISTS _menu_symbol_rows)
    string(REGEX REPLACE " .*" "" _symbol "${_row}")
    list(APPEND _menu_symbols "${_symbol}")
  endforeach()
  file(STRINGS "${SM64DS_PORT_DIR}/menu/dependency_symbols.txt" _menu_dependencies)
  # Adapt a fresh tree, then retain timestamps of identical published sources.
  # Synchronization still removes stale units when the closure changes.
  set(_menu_stage "${_menu_output}.staging")
  file(REMOVE_RECURSE "${_menu_stage}")
  execute_process(COMMAND "${PYTHON}" "${SM64DS_PORT_DIR}/tools/hostgen.py"
    --decomp "${_menu_root}" --out "${_menu_stage}" --gcc-local-externs --native-primary-vptrs
    ${_menu_symbols} ${_menu_dependencies} RESULT_VARIABLE _menu_generated OUTPUT_QUIET)
  if(NOT _menu_generated EQUAL 0)
    message(FATAL_ERROR "Retail menu source generation failed")
  endif()
  execute_process(COMMAND "${PYTHON}" "${SM64DS_PORT_DIR}/tools/menu_sources.py"
    --tree "${_menu_stage}" RESULT_VARIABLE _menu_adapted)
  if(NOT _menu_adapted EQUAL 0)
    message(FATAL_ERROR "Retail menu ABI adaptation failed")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_menu_root}/config/arm9/overlays/ov007/symbols.txt"
    "${SM64DS_PORT_DIR}/tools/menu_sources.py"
    "${SM64DS_PORT_DIR}/tools/menu_storage.py"
    "${SM64DS_PORT_DIR}/menu/dependency_symbols.txt"
    "${SM64DS_PORT_DIR}/tools/sync_generated_tree.py"
    "${SM64DS_PORT_DIR}/tools/hostgen.py"
    "${SM64DS_PORT_DIR}/tools/native_vtable_stores.py"
    "${SM64DS_PORT_DIR}/tools/native_vtable_layouts.json")
  execute_process(COMMAND "${PYTHON}" "${SM64DS_PORT_DIR}/tools/menu_storage.py"
    "${_menu_root}" "${_menu_stage}/menu_storage.cpp"
    RESULT_VARIABLE _menu_storage OUTPUT_QUIET)
  if(NOT _menu_storage EQUAL 0)
    message(FATAL_ERROR "Retail menu storage generation failed")
  endif()
  execute_process(COMMAND "${PYTHON}" "${SM64DS_PORT_DIR}/tools/sync_generated_tree.py"
    "${_menu_stage}" "${_menu_output}" --preserve local_input_transfer.c
    RESULT_VARIABLE _menu_published)
  if(NOT _menu_published EQUAL 0)
    message(FATAL_ERROR "Retail menu source publication failed")
  endif()
  file(GLOB_RECURSE _menu_sources "${_menu_output}/src/*.cpp")
  # Native local transfer uses three symbols that alias subranges of1154.
  # Express those aliases through their shared base before C optimization.
  set(_local_input "${_menu_output}/local_input_transfer.c")
  add_custom_command(OUTPUT "${_local_input}"
    COMMAND "${PYTHON}" "${SM64DS_PORT_DIR}/tools/local_input_source.py"
      "${_menu_root}/src/func_0203e0ac.c" "${_local_input}"
    DEPENDS "${_menu_root}/src/func_0203e0ac.c"
      "${SM64DS_PORT_DIR}/tools/local_input_source.py" VERBATIM)
  add_library(sm64ds_menu OBJECT EXCLUDE_FROM_ALL ${_menu_sources}
    "${_menu_output}/menu_storage.cpp" "${SM64DS_PORT_DIR}/menu/lifecycle.cpp"
    "${SM64DS_PORT_DIR}/menu/primitives.cpp" "${SM64DS_PORT_DIR}/menu/input.cpp"
    "${_local_input}")
  target_link_libraries(sm64ds_menu PRIVATE ntr)
  target_compile_definitions(sm64ds_menu PRIVATE SM64DS_PLATFORM_PC SM64DS_NATIVE_SHADOW)
  target_include_directories(sm64ds_menu PRIVATE
    "${SM64DS_PORT_DIR}" "${_menu_root}/include")
  target_compile_options(sm64ds_menu PRIVATE
    -fno-strict-aliasing -Wno-narrowing -Werror=return-type
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
endif()
