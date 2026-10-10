# Retail mini-game base plus the reviewed slices listed in minigame_imports.inc.
# Overlay6 sources/storage/ctors are complete; runtime admission remains explicit.
if(NINTENDO_3DS)
  set(_wanted_root "${SM64DS_DECOMP_DIR}")
  set(_wanted_output "${CMAKE_CURRENT_BINARY_DIR}/wanted-native")
  file(STRINGS "${_wanted_root}/config/arm9/overlays/ov004/symbols.txt"
    _wanted_rows REGEX " kind:function")
  set(_wanted_symbols)
  foreach(_row IN LISTS _wanted_rows)
    string(REGEX REPLACE " .*" "" _symbol "${_row}")
    list(APPEND _wanted_symbols "${_symbol}")
  endforeach()
  file(STRINGS "${SM64DS_PORT_DIR}/menu/wanted_dependencies.txt" _wanted_dependencies)
  set(_wanted_stage "${_wanted_output}.staging")
  file(REMOVE_RECURSE "${_wanted_stage}")
  execute_process(COMMAND "${PYTHON}" "${SM64DS_PORT_DIR}/tools/hostgen.py"
    --decomp "${_wanted_root}" --out "${_wanted_stage}" --gcc-local-externs --native-primary-vptrs
    ${_wanted_symbols} ${_wanted_dependencies} RESULT_VARIABLE _wanted_result OUTPUT_QUIET)
  if(NOT _wanted_result EQUAL 0)
    message(FATAL_ERROR "Wanted source generation failed")
  endif()
  execute_process(COMMAND "${PYTHON}" "${SM64DS_PORT_DIR}/tools/minigame_sources.py"
    "${_wanted_stage}" RESULT_VARIABLE _wanted_result)
  if(NOT _wanted_result EQUAL 0)
    message(FATAL_ERROR "Wanted ABI adaptation failed")
  endif()
  execute_process(COMMAND "${PYTHON}" "${SM64DS_PORT_DIR}/tools/prepare_overlay6_native.py"
    "${_wanted_root}" "${_wanted_stage}" "${CMAKE_CXX_COMPILER}"
    RESULT_VARIABLE _wanted_result)
  if(NOT _wanted_result EQUAL 0)
    message(FATAL_ERROR "Overlay6 source/storage/ctor/ownership preparation failed")
  endif()
  execute_process(COMMAND "${PYTHON}" "${SM64DS_PORT_DIR}/tools/sync_generated_tree.py"
    "${_wanted_stage}" "${_wanted_output}" RESULT_VARIABLE _wanted_result)
  if(NOT _wanted_result EQUAL 0)
    message(FATAL_ERROR "Wanted source publication failed")
  endif()
  file(GLOB_RECURSE _wanted_sources "${_wanted_output}/src/*.cpp")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SM64DS_PORT_DIR}/menu/wanted_dependencies.txt"
    "${SM64DS_PORT_DIR}/menu/wanted_storage_roots.txt"
    "${SM64DS_PORT_DIR}/menu/wanted_oam_consumers.txt"
    "${SM64DS_PORT_DIR}/menu/wanted_native_bindings.txt"
    "${SM64DS_PORT_DIR}/tools/minigame_sources.py"
    "${SM64DS_PORT_DIR}/tools/hostgen.py"
    "${SM64DS_PORT_DIR}/tools/native_vtable_stores.py"
    "${SM64DS_PORT_DIR}/tools/native_vtable_layouts.json"
    "${SM64DS_PORT_DIR}/tools/sync_generated_tree.py"
    "${SM64DS_PORT_DIR}/tools/minigame_storage.py"
    "${SM64DS_PORT_DIR}/tools/minigame_oam_extent_audit.py"
    "${SM64DS_PORT_DIR}/tools/minigame_data_audit.py")
  include("${_wanted_output}/vtable-link-options.cmake")
  target_link_options(smoke_player PRIVATE ${_wanted_vtable_link_options})
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SM64DS_PORT_DIR}/menu/overlay6_native_policy.json"
    "${SM64DS_PORT_DIR}/menu/minigame_imports.inc"
    "${SM64DS_PORT_DIR}/tools/prepare_overlay6_native.py"
    "${SM64DS_PORT_DIR}/tools/verify_overlay6_native.py"
    "${SM64DS_PORT_DIR}/tools/minigame_batch_plan.py"
    "${SM64DS_PORT_DIR}/tools/batch_native_abi.py"
    "${SM64DS_PORT_DIR}/tools/initialized_data_owner.py"
    "${SM64DS_PORT_DIR}/tools/retail_rtti_bridge.py"
    "${SM64DS_PORT_DIR}/tools/minigame_initializer_audit.py"
    "${SM64DS_PORT_DIR}/tools/minigame_imports.py")
  add_library(sm64ds_wanted OBJECT EXCLUDE_FROM_ALL ${_wanted_sources}
    "${_wanted_output}/storage.cpp" menu/wanted_lifecycle.cpp)
  target_link_libraries(sm64ds_wanted PRIVATE ntr)
  target_include_directories(sm64ds_wanted PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${_wanted_root}/include")
  target_compile_definitions(sm64ds_wanted PRIVATE SM64DS_PLATFORM_PC SM64DS_NATIVE_SHADOW)
  target_compile_options(sm64ds_wanted PRIVATE -fno-strict-aliasing
    -Wno-narrowing -Werror=return-type $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
  target_sources(smoke_player PRIVATE $<TARGET_OBJECTS:sm64ds_wanted>)
  add_custom_command(TARGET smoke_player POST_BUILD
    COMMAND "${PYTHON}" "${SM64DS_PORT_DIR}/tools/verify_overlay6_native.py"
      "${_wanted_root}" "$<TARGET_FILE:smoke_player>"
      "${_wanted_output}/batch-audit/storage-audit.json"
      "${SM64DS_PORT_DIR}/menu/overlay6_native_policy.json"
      "${SM64DS_PORT_DIR}/menu/wanted_native_bindings.txt"
      "${_wanted_output}/linked-ABI-gate.json"
    COMMENT "Verifying overlay6 linked native ABI/storage contract")
endif()
