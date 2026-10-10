# Retail Rec Room selection scene; individual games in overlay6 remain separate.
if(NINTENDO_3DS)
  sm64ds_hostgen_symbols(MINIGAME_MENU_NATIVE dScMiniGm_c_classInit
    ExitMinigameMenu func_02013568 func_02013580 func_02013598 func_020135d0
    func_02013638 func_02019028 func_020233f4 func_0202ec9c)
  set(MINIGAME_MENU_STORAGE "${CMAKE_CURRENT_BINARY_DIR}/minigame_menu_storage.cpp")
  add_custom_command(OUTPUT "${MINIGAME_MENU_STORAGE}"
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/minigame_menu_storage.py"
      "${SM64DS_DECOMP_DIR}" "${MINIGAME_MENU_STORAGE}"
    DEPENDS tools/minigame_menu_storage.py ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0005.bin
      ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov005/symbols.txt
      ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov005/relocs.txt)
  add_library(sm64ds_minigame_menu OBJECT EXCLUDE_FROM_ALL ${MINIGAME_MENU_NATIVE}
    "${MINIGAME_MENU_STORAGE}" menu/minigame_lifecycle.cpp)
  target_link_libraries(sm64ds_minigame_menu PRIVATE ntr)
  target_include_directories(sm64ds_minigame_menu PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
  target_compile_options(sm64ds_minigame_menu PRIVATE -fno-strict-aliasing
    -Wno-narrowing $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
  target_sources(smoke_player PRIVATE $<TARGET_OBJECTS:sm64ds_minigame_menu>)
endif()
