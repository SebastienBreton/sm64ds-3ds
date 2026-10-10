cmake_minimum_required(VERSION 3.16)
if(CMAKE_TOOLCHAIN_FILE MATCHES "3DS")
    # See platform/3ds/cc-launcher.sh: weakens HAL forwarders after compiling.
    set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/cc-launcher.sh")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/cc-launcher.sh")
endif()

project(sm64ds-port C CXX)
# The port lives in its own repository (sm64ds-3ds/port) and builds against the
# decomp checked out next to it (git submodule sm64ds-3ds/sm64ds-decomp).
set(SM64DS_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
get_filename_component(_sm64ds_default_decomp "${CMAKE_CURRENT_SOURCE_DIR}/../sm64ds-decomp" ABSOLUTE)
set(SM64DS_DECOMP_DIR "${_sm64ds_default_decomp}" CACHE PATH
    "sm64ds-decomp checkout the port builds against (default: ../sm64ds-decomp)")
get_filename_component(SM64DS_DECOMP_DIR "${SM64DS_DECOMP_DIR}" ABSOLUTE)
if(NOT EXISTS "${SM64DS_DECOMP_DIR}/include/types.h" OR NOT EXISTS "${SM64DS_DECOMP_DIR}/tools/srcpath.py")
    message(FATAL_ERROR "SM64DS_DECOMP_DIR=${SM64DS_DECOMP_DIR} is not an sm64ds-decomp checkout "
        "(run `git submodule update --init` or pass -DSM64DS_DECOMP_DIR=...)")
endif()
# Port tools resolve the decomp through this variable when no path is passed.
set(ENV{SM64DS_DECOMP_DIR} "${SM64DS_DECOMP_DIR}")
# Gate manifests (slice_gate*.txt) list decomp-relative paths (src/...); entries
# spelled port/... name files of this port directory, not the decomp's own port/.
macro(sm64ds_manifest_source var)
    if("${${var}}" MATCHES "^port/")
        string(REGEX REPLACE "^port/" "${SM64DS_PORT_DIR}/" ${var} "${${var}}")
    else()
        set(${var} "${SM64DS_DECOMP_DIR}/${${var}}")
    endif()
endmacro()
# These port files compile decomp TUs in place (#include "src/<file>"), so they
# see the decomp root on their include path.
set_source_files_properties(hal/actor_ctor_bridge.cpp hal/bg_ctor_bridge.cpp tests/smoke_sound_storage.cpp
    PROPERTIES INCLUDE_DIRECTORIES "${SM64DS_DECOMP_DIR}")
option(SM64DS_EXPERIMENTAL_GPU_COMPOSITE
    "Build the GPU resident screen compositor with capability-based CPU fallback" ON)
if(NINTENDO_3DS)
    # The game is built with mwccarm -char signed (and the Windows port gets
    # signed char from MSVC); GCC on ARM defaults to unsigned char.
    add_compile_options(-fsigned-char)
endif()
include(CTest)
# file(STRINGS) does not automatically register its input as a configure
# dependency. Reconfigure when a gate slice changes, or additions are ignored.
file(GLOB PORT_SLICE_MANIFESTS "${CMAKE_CURRENT_SOURCE_DIR}/slice_gate*.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PORT_SLICE_MANIFESTS})
if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MAP")
    add_compile_options(/Oy-)
endif()
# Code generators are Python scripts; `python` is not on every host's PATH.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PYTHON "${Python3_EXECUTABLE}")

# Gate 1: compile the manifest slice out of ../src plus the HAL, link the
# smoke runner. 32-bit host only -- platform.h enforces it at compile time.

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# The matching toolchain selects C++ by an exact first-line //cpp marker even
# when a source keeps its historical .c path. Teach the host build the same
# rule so a verified language-mode migration cannot silently break a slice.
# GCC emits the complete (C1/D1) and base (C2/D2) object variants of a ctor or
# dtor from ONE definition, while the matching build keeps each variant in its
# own file holding the same source. On GCC hosts keep only the C1/D1 file.
function(sm64ds_drop_gcc_variant_twins source_var)
    if(MSVC)
        return()
    endif()
    set(kept "")
    foreach(source IN LISTS ${source_var})
        if(source MATCHES "^(.*)C2E(.*)$")
            if("${CMAKE_MATCH_1}C1E${CMAKE_MATCH_2}" IN_LIST ${source_var})
                continue()
            endif()
        elseif(source MATCHES "^(.*)D2E(.*)$")
            if("${CMAKE_MATCH_1}D1E${CMAKE_MATCH_2}" IN_LIST ${source_var})
                continue()
            endif()
        endif()
        list(APPEND kept "${source}")
    endforeach()
    set(${source_var} "${kept}" PARENT_SCOPE)
endfunction()

function(sm64ds_honor_cpp_markers source_var)
    foreach(source IN LISTS ${source_var})
        if(source MATCHES "\\.c$")
            file(STRINGS "${source}" first_line LIMIT_COUNT 1)
            if(first_line STREQUAL "//cpp")
                set_source_files_properties("${source}" PROPERTIES LANGUAGE CXX)
            endif()
        endif()
    endforeach()
endfunction()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/slice_gate1.txt" SLICE_LINES)
set(SLICE_SOURCES "")
foreach(line IN LISTS SLICE_LINES)
    string(STRIP "${line}" line)
    if(line MATCHES "^#" OR line STREQUAL "")
        continue()
    endif()
    sm64ds_manifest_source(line)
    list(APPEND SLICE_SOURCES "${line}")
endforeach()
sm64ds_drop_gcc_variant_twins(SLICE_SOURCES)
sm64ds_honor_cpp_markers(SLICE_SOURCES)

add_executable(smoke
    tests/smoke.cpp
    hal/cstd_div.c
    hal/os_time.cpp
    hal/shims.cpp
    hal/mem_delete2.cpp
    ${SLICE_SOURCES})

target_include_directories(smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")

target_compile_definitions(smoke PRIVATE SM64DS_PLATFORM_PC)
if(MSVC)
    # The decomp C uses guest idioms MSVC warns about; keep the build quiet
    # enough that a NEW warning is visible. /W3 stays on.
    target_compile_options(smoke PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# Gate 2: the allocator family + torture smoke, its own binary so gate 1
# stays a stable baseline.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/slice_gate2.txt" SLICE2_LINES)
set(SLICE2_SOURCES "")
foreach(line IN LISTS SLICE2_LINES)
    string(STRIP "${line}" line)
    if(line MATCHES "^#" OR line STREQUAL "")
        continue()
    endif()
    sm64ds_manifest_source(line)
    list(APPEND SLICE2_SOURCES "${line}")
endforeach()
sm64ds_drop_gcc_variant_twins(SLICE2_SOURCES)
sm64ds_honor_cpp_markers(SLICE2_SOURCES)

add_executable(smoke_heap tests/smoke_heap.cpp hal/heap_globals.cpp ${SLICE2_SOURCES})
target_include_directories(smoke_heap PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_compile_definitions(smoke_heap PRIVATE SM64DS_PLATFORM_PC)
if(MSVC)
    target_compile_options(smoke_heap PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# Gate 3a: root heap + Memory:: layer over the gate-2 allocator.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/slice_gate3a.txt" SLICE3A_LINES)
set(SLICE3A_SOURCES "")
foreach(line IN LISTS SLICE3A_LINES)
    string(STRIP "${line}" line)
    if(line MATCHES "^#" OR line STREQUAL "")
        continue()
    endif()
    sm64ds_manifest_source(line)
    list(APPEND SLICE3A_SOURCES "${line}")
endforeach()
sm64ds_drop_gcc_variant_twins(SLICE3A_SOURCES)
sm64ds_honor_cpp_markers(SLICE3A_SOURCES)

add_executable(smoke_roots tests/smoke_roots.cpp
    hal/heap_globals.cpp hal/os_arena.cpp hal/heap_vtable.cpp hal/mem_delete2.cpp
    ${SLICE2_SOURCES} ${SLICE3A_SOURCES})
target_include_directories(smoke_roots PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_compile_definitions(smoke_roots PRIVATE SM64DS_PLATFORM_PC)
if(MSVC)
    target_compile_options(smoke_roots PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# The ntr platform layer: imported host DS hardware (see ntr/README.md).
# Compiled as its own lib so every gate binary can link the pieces it needs.
add_library(ntr STATIC
    ntr/io.cpp ntr/gx.cpp ntr/texture.cpp ntr/ppu.cpp ntr/bitmap.cpp ntr/lz77.cpp
    ntr/rt.cpp ntr/runtime.cpp ntr/fs.cpp ntr/bmd.cpp
    ntr/vram_banks.cpp ntr/vram_bus.cpp ntr/display_capture.cpp)
target_include_directories(ntr PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ntr/include")
# GX mesh headers share the existing platform geometry declarations.
target_include_directories(ntr PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(ntr PRIVATE /wd4311 /wd4312)
endif()

# Gate 3b: SharedFilePtr over the host card seam; assets come from the
# repo's extracted/ tree + asset catalog, root baked in (env-overridable).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/slice_gate3b.txt" SLICE3B_LINES)
set(SLICE3B_SOURCES "")
foreach(line IN LISTS SLICE3B_LINES)
    string(STRIP "${line}" line)
    if(line MATCHES "^#" OR line STREQUAL "")
        continue()
    endif()
    sm64ds_manifest_source(line)
    list(APPEND SLICE3B_SOURCES "${line}")
endforeach()
sm64ds_drop_gcc_variant_twins(SLICE3B_SOURCES)
sm64ds_honor_cpp_markers(SLICE3B_SOURCES)

get_filename_component(PORT_REPO_ROOT_ABS "${SM64DS_DECOMP_DIR}" ABSOLUTE)

# Resolve hostgen symbols through the decomp's production enrollment table.  A
# reconstructed translation unit can own several symbols under one source path, so
# src/<symbol>.c|.cpp probing is no longer a valid source lookup.  Dedupe by resolved
# path: two requested symbols in the same TU must compile that TU once, not twice.
# Resolve the official enrollment index once per configure, rather than spawning
# Python and reparsing every module for each requested symbol.
execute_process(COMMAND ${PYTHON} "${PORT_REPO_ROOT_ABS}/tools/srcpath.py" --index
    WORKING_DIRECTORY "${PORT_REPO_ROOT_ABS}"
    RESULT_VARIABLE hostgen_index_result OUTPUT_VARIABLE HOSTGEN_SOURCE_INDEX
    ERROR_VARIABLE hostgen_index_error)
if(NOT hostgen_index_result EQUAL 0)
    message(FATAL_ERROR "hostgen source index failed: ${hostgen_index_error}")
endif()
function(sm64ds_hostgen_symbols out_var)
    set(generated "")
    set(seen_sources "")
    foreach(sym IN LISTS ARGN)
        string(JSON sym_rel ERROR_VARIABLE resolve_error GET "${HOSTGEN_SOURCE_INDEX}" "${sym}")
        if(NOT resolve_error STREQUAL "NOTFOUND")
            message(FATAL_ERROR "hostgen source resolution failed for ${sym}: ${resolve_error}")
        endif()
        set(sym_src "${PORT_REPO_ROOT_ABS}/${sym_rel}")
        if(NOT EXISTS "${sym_src}")
            message(FATAL_ERROR "hostgen resolved ${sym} to missing ${sym_rel}")
        endif()
        list(FIND seen_sources "${sym_src}" seen_index)
        if(NOT seen_index EQUAL -1)
            continue()
        endif()
        list(APPEND seen_sources "${sym_src}")
        string(REGEX REPLACE "\\.(c|cpp)$" ".cpp" sym_out_rel "${sym_rel}")
        set(sym_out "${CMAKE_BINARY_DIR}/host-src/${sym_out_rel}")
        set(hostgen_abi_args "")
        if(NINTENDO_3DS AND (sym_rel STREQUAL "src/actors/Player.cpp" OR sym_rel STREQUAL "src/actors/daBmb_c.cpp" OR sym_rel STREQUAL "src/actors/da1up_c.cpp" OR sym_rel STREQUAL "src/actors/daCoin_c.cpp" OR sym_rel STREQUAL "src/actors/daMip_c.cpp" OR sym_rel STREQUAL "src/game/actors/d_a_holhei.cpp" OR sym_rel STREQUAL "src/game/actors/d_a_nknk.cpp" OR sym_rel STREQUAL "src/actors/daRNk_c.cpp" OR sym_rel STREQUAL "src/actors/daRFlag_c.cpp" OR sym_rel STREQUAL "src/actors/daJango_c.cpp"))
            set(hostgen_abi_args --gcc-local-externs)
        endif()
        if(NINTENDO_3DS)
            list(APPEND hostgen_abi_args --native-primary-vptrs)
        endif()
        set(hostgen_post_command "")
        set(hostgen_post_dependencies "")
        if(NINTENDO_3DS AND sym STREQUAL "func_020197b8")
            set(hostgen_post_command COMMAND ${PYTHON}
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/observed_cycle_hook.py" "${sym_out}")
            set(hostgen_post_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/tools/observed_cycle_hook.py")
        endif()
        add_custom_command(OUTPUT "${sym_out}"
            COMMAND ${PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/tools/hostgen.py" --decomp "${SM64DS_DECOMP_DIR}"
                    --out "${CMAKE_BINARY_DIR}/host-src" ${hostgen_abi_args} ${sym}
            ${hostgen_post_command}
            DEPENDS "${sym_src}" ${hostgen_post_dependencies}
                    "${PORT_REPO_ROOT_ABS}/tools/srcpath.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/hostgen.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/native_vtable_stores.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/native_vtable_layouts.json"
                    "${PORT_REPO_ROOT_ABS}/include/nitro/hw/registers.h"
            COMMENT "hostgen ${sym_rel}")
        list(APPEND generated "${sym_out}")
    endforeach()
    set(${out_var} "${generated}" PARENT_SCOPE)
endfunction()

# Gate 4a: the game's display-list submission machinery over the ntr GX.
# These src/ files touch MMIO textually, so they build from hostgen-emitted
# copies under the build tree -- src/ itself is never edited.
set(GATE4A_SYMS
    func_0205532c func_02055388 func_020553a4 func_02055780
    func_0205a358 func_0205a290 func_0205a21c func_02056e98 func_02059fa8)
sm64ds_hostgen_symbols(GATE4A_GEN ${GATE4A_SYMS})

add_executable(smoke_gx tests/smoke_gx.cpp ${GATE4A_GEN})
target_include_directories(smoke_gx PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_link_libraries(smoke_gx PRIVATE ntr)
target_compile_definitions(smoke_gx PRIVATE SM64DS_PLATFORM_PC
    PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")
if(MSVC)
    target_compile_options(smoke_gx PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# Gate 4b: the whole Model pipeline. Adds the material-bind and matrix
# feeders to the hostgen set and the Model/render slice on top of everything.
set(GATE4B_SYMS func_02044b30 Geometry_MatrixMultiply3x3 func_02055998
    func_02044534 func_0204488c NormalizeVec3)
sm64ds_hostgen_symbols(GATE4B_GEN ${GATE4B_SYMS})

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/slice_gate4b.txt" SLICE4B_LINES)
set(SLICE4B_SOURCES "")
foreach(line IN LISTS SLICE4B_LINES)
    string(STRIP "${line}" line)
    if(line MATCHES "^#" OR line STREQUAL "")
        continue()
    endif()
    sm64ds_manifest_source(line)
    list(APPEND SLICE4B_SOURCES "${line}")
endforeach()
sm64ds_drop_gcc_variant_twins(SLICE4B_SOURCES)
sm64ds_honor_cpp_markers(SLICE4B_SOURCES)

# ROM constants (sine table, default matrices): generated locally from the
# extracted arm9 image so Nintendo bytes never enter the repo.
set(ROMDATA_C "${CMAKE_BINARY_DIR}/host-src/romdata.c")
add_custom_command(OUTPUT "${ROMDATA_C}"
    COMMAND ${PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/tools/romdata.py" "${ROMDATA_C}" "${SM64DS_DECOMP_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/romdata.py"
    COMMENT "romdata from extracted/arm9_dec.bin")

add_executable(smoke_model tests/smoke_model.cpp
    hal/heap_globals.cpp hal/os_arena.cpp hal/heap_vtable.cpp hal/fs.cpp
    hal/model_host.cpp hal/gx_upload_bridge.cpp hal/ctor_bridge.cpp
    ${SLICE2_SOURCES} ${SLICE3A_SOURCES} ${SLICE3B_SOURCES} ${SLICE4B_SOURCES}
    ${GATE4A_GEN} ${GATE4B_GEN} "${ROMDATA_C}")
target_include_directories(smoke_model PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_link_libraries(smoke_model PRIVATE ntr)
target_compile_definitions(smoke_model PRIVATE SM64DS_PLATFORM_PC
    PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")
if(MSVC)
    target_compile_options(smoke_model PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# Gate 5: the fiber frame loop -- game-shaped frames, host-rastered.
add_executable(smoke_frames tests/smoke_frames.cpp
    hal/heap_globals.cpp hal/os_arena.cpp hal/heap_vtable.cpp hal/fs.cpp
    hal/model_host.cpp hal/gx_upload_bridge.cpp hal/ctor_bridge.cpp
    ${SLICE2_SOURCES} ${SLICE3A_SOURCES} ${SLICE3B_SOURCES} ${SLICE4B_SOURCES}
    ${GATE4A_GEN} ${GATE4B_GEN} "${ROMDATA_C}")
target_include_directories(smoke_frames PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_link_libraries(smoke_frames PRIVATE ntr)
target_compile_definitions(smoke_frames PRIVATE SM64DS_PLATFORM_PC
    PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")
if(MSVC)
    target_compile_options(smoke_frames PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# Gate 4d: the catalog soak -- every model through the game pipeline.
add_executable(smoke_soak tests/smoke_soak.cpp
    hal/heap_globals.cpp hal/os_arena.cpp hal/heap_vtable.cpp hal/fs.cpp
    hal/model_host.cpp hal/gx_upload_bridge.cpp hal/ctor_bridge.cpp
    ${SLICE2_SOURCES} ${SLICE3A_SOURCES} ${SLICE3B_SOURCES} ${SLICE4B_SOURCES}
    ${GATE4A_GEN} ${GATE4B_GEN} "${ROMDATA_C}")
target_include_directories(smoke_soak PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_link_libraries(smoke_soak PRIVATE ntr)
target_compile_definitions(smoke_soak PRIVATE SM64DS_PLATFORM_PC
    PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")
if(MSVC)
    target_compile_options(smoke_soak PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# Gate 8: collision -- the octree walk over real KCL data. cstd::sqrt
# drives the hardware sqrt unit through literal MMIO -> hostgen.
set(GATE8_SYMS _ZN4cstd4sqrtEy _ZN4cstd11fdiv_resultEv _ZN4cstd16reciprocal_asyncE5Fix12IiE
    Matrix4x3_ApplyInPlaceToScale)
sm64ds_hostgen_symbols(GATE8_GEN ${GATE8_SYMS})

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/slice_gate8.txt" SLICE8_LINES)
set(SLICE8_SOURCES "")
foreach(line IN LISTS SLICE8_LINES)
    string(STRIP "${line}" line)
    if(line MATCHES "^#" OR line STREQUAL "")
        continue()
    endif()
    sm64ds_manifest_source(line)
    list(APPEND SLICE8_SOURCES "${line}")
endforeach()
sm64ds_drop_gcc_variant_twins(SLICE8_SOURCES)
sm64ds_honor_cpp_markers(SLICE8_SOURCES)

add_executable(smoke_clsn tests/smoke_clsn.cpp
    hal/heap_globals.cpp hal/os_arena.cpp hal/heap_vtable.cpp hal/fs.cpp
    hal/model_host.cpp hal/gx_upload_bridge.cpp hal/ctor_bridge.cpp hal/cstd_div.c
    hal/clsn_vtable.cpp hal/bg_ctor_bridge.cpp
    ${SLICE2_SOURCES} ${SLICE3A_SOURCES} ${SLICE3B_SOURCES} ${SLICE4B_SOURCES}
    ${SLICE8_SOURCES} ${GATE4A_GEN} ${GATE4B_GEN} ${GATE8_GEN} "${ROMDATA_C}")
target_include_directories(smoke_clsn PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_link_libraries(smoke_clsn PRIVATE ntr)
target_compile_definitions(smoke_clsn PRIVATE SM64DS_PLATFORM_PC
    PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")
if(MSVC)
    target_compile_options(smoke_clsn PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# Gate 7: ModelAnim -- the game owns frame progression.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/slice_gate7.txt" SLICE7_LINES)
set(SLICE7_SOURCES "")
foreach(line IN LISTS SLICE7_LINES)
    string(STRIP "${line}" line)
    if(line MATCHES "^#" OR line STREQUAL "")
        continue()
    endif()
    sm64ds_manifest_source(line)
    list(APPEND SLICE7_SOURCES "${line}")
endforeach()
sm64ds_drop_gcc_variant_twins(SLICE7_SOURCES)
sm64ds_honor_cpp_markers(SLICE7_SOURCES)

add_executable(smoke_modelanim tests/smoke_modelanim.cpp
    "${SM64DS_DECOMP_DIR}/src/_ZN10ModelAnim2C1Ev.cpp"
    "${SM64DS_DECOMP_DIR}/src/_ZN10ModelAnim2D1Ev.cpp"
    hal/heap_globals.cpp hal/os_arena.cpp hal/heap_vtable.cpp hal/fs.cpp
    hal/model_host.cpp hal/gx_upload_bridge.cpp hal/ctor_bridge.cpp hal/anim_bridge.cpp hal/anim_ctor_bridge.cpp
    ${SLICE2_SOURCES} ${SLICE3A_SOURCES} ${SLICE3B_SOURCES} ${SLICE4B_SOURCES}
    ${SLICE7_SOURCES} ${GATE4A_GEN} ${GATE4B_GEN} "${ROMDATA_C}")
target_include_directories(smoke_modelanim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_link_libraries(smoke_modelanim PRIVATE ntr)
target_compile_definitions(smoke_modelanim PRIVATE SM64DS_PLATFORM_PC
    PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")
if(MSVC)
    target_compile_options(smoke_modelanim PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# Gate 6: the 2D sprite engine's software side. OAM::Load reads POWCNT
# textually -> hostgen; the rest of the slice is plain.
set(GATE6_SYMS _ZN3OAM4LoadEv)
sm64ds_hostgen_symbols(GATE6_GEN ${GATE6_SYMS})

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/slice_gate6.txt" SLICE6_LINES)
set(SLICE6_SOURCES "")
foreach(line IN LISTS SLICE6_LINES)
    string(STRIP "${line}" line)
    if(line MATCHES "^#" OR line STREQUAL "")
        continue()
    endif()
    if(line MATCHES "_ZN3OAM4LoadEv")
        continue()
    endif()
    sm64ds_manifest_source(line)
    list(APPEND SLICE6_SOURCES "${line}")
endforeach()
sm64ds_drop_gcc_variant_twins(SLICE6_SOURCES)
sm64ds_honor_cpp_markers(SLICE6_SOURCES)

add_executable(smoke_oam tests/smoke_oam.cpp
    hal/heap_globals.cpp hal/os_arena.cpp hal/heap_vtable.cpp hal/fs.cpp
    hal/model_host.cpp hal/gx_upload_bridge.cpp hal/ctor_bridge.cpp hal/oam_bridge.cpp
    ${SLICE2_SOURCES} ${SLICE3A_SOURCES} ${SLICE3B_SOURCES} ${SLICE4B_SOURCES}
    ${SLICE6_SOURCES} ${GATE4A_GEN} ${GATE4B_GEN} ${GATE6_GEN} "${ROMDATA_C}")
target_include_directories(smoke_oam PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_link_libraries(smoke_oam PRIVATE ntr)
target_compile_definitions(smoke_oam PRIVATE SM64DS_PLATFORM_PC
    PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")
if(MSVC)
    target_compile_options(smoke_oam PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# Gate 5b: the animation soak -- every compatible model+BCA pair.
add_executable(smoke_soak_anim tests/smoke_soak_anim.cpp
    hal/heap_globals.cpp hal/os_arena.cpp hal/heap_vtable.cpp hal/fs.cpp
    hal/model_host.cpp hal/gx_upload_bridge.cpp hal/ctor_bridge.cpp
    ${SLICE2_SOURCES} ${SLICE3A_SOURCES} ${SLICE3B_SOURCES} ${SLICE4B_SOURCES}
    ${GATE4A_GEN} ${GATE4B_GEN} "${ROMDATA_C}")
target_include_directories(smoke_soak_anim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_link_libraries(smoke_soak_anim PRIVATE ntr)
target_compile_definitions(smoke_soak_anim PRIVATE SM64DS_PLATFORM_PC
    PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")
if(MSVC)
    target_compile_options(smoke_soak_anim PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# Gate 4c: animation -- same slice (the 4c files live in slice_gate4b.txt),
# different driver: the piano posed by its BCA through the game's walk.
add_executable(smoke_anim tests/smoke_anim.cpp
    hal/heap_globals.cpp hal/os_arena.cpp hal/heap_vtable.cpp hal/fs.cpp
    hal/model_host.cpp hal/gx_upload_bridge.cpp hal/ctor_bridge.cpp
    ${SLICE2_SOURCES} ${SLICE3A_SOURCES} ${SLICE3B_SOURCES} ${SLICE4B_SOURCES}
    ${GATE4A_GEN} ${GATE4B_GEN} "${ROMDATA_C}")
target_include_directories(smoke_anim PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_link_libraries(smoke_anim PRIVATE ntr)
target_compile_definitions(smoke_anim PRIVATE SM64DS_PLATFORM_PC
    PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")
if(MSVC)
    target_compile_options(smoke_anim PRIVATE /wd4311 /wd4312 /wd4068)
endif()

add_executable(smoke_fs tests/smoke_fs.cpp
    "${SM64DS_DECOMP_DIR}/src/func_0205d368.c"
    hal/heap_globals.cpp hal/os_arena.cpp hal/heap_vtable.cpp hal/fs.cpp
    hal/mem_delete2.cpp
    ${SLICE2_SOURCES} ${SLICE3A_SOURCES} ${SLICE3B_SOURCES})
target_link_libraries(smoke_fs PRIVATE ntr)
target_include_directories(smoke_fs PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_compile_definitions(smoke_fs PRIVATE SM64DS_PLATFORM_PC
    PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")
if(MSVC)
    target_compile_options(smoke_fs PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# Gate 9: the actor framework -- ArrowSignRight's full lifecycle.
# One .c file carries C++ syntax (mwccarm accepted it); hostgen's
# emit-as-cpp with extern "C" wrap makes it legal.
set(GATE9_SYMS _ZN7fBase_c21AfterCleanupResourcesEj _ZN6Player16IncMegaKillCountEv IPCSend)
sm64ds_hostgen_symbols(GATE9_GEN ${GATE9_SYMS})

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/slice_gate9.txt" SLICE9_LINES)
set(SLICE9_SOURCES "")
foreach(line IN LISTS SLICE9_LINES)
    string(STRIP "${line}" line)
    if(line MATCHES "^#" OR line STREQUAL "")
        continue()
    endif()
    sm64ds_manifest_source(line)
    list(APPEND SLICE9_SOURCES "${line}")
endforeach()
sm64ds_drop_gcc_variant_twins(SLICE9_SOURCES)
sm64ds_honor_cpp_markers(SLICE9_SOURCES)

# Deduplicate GCC constructor/destructor variants across the complete union
# of slices, since a C1 in gate 8 and its C2 in gate 9 otherwise both link.
set(ACTOR_GAME_SOURCES ${SLICE2_SOURCES} ${SLICE3A_SOURCES} ${SLICE3B_SOURCES}
    ${SLICE4B_SOURCES} ${SLICE7_SOURCES} ${SLICE8_SOURCES} ${SLICE9_SOURCES})
list(REMOVE_DUPLICATES ACTOR_GAME_SOURCES)
sm64ds_drop_gcc_variant_twins(ACTOR_GAME_SOURCES)

add_executable(smoke_actor tests/smoke_actor.cpp unmatched/crash_diagnostics.cpp
    hal/heap_globals.cpp hal/os_arena.cpp hal/heap_vtable.cpp hal/fs.cpp
    hal/model_host.cpp hal/gx_upload_bridge.cpp hal/ctor_bridge.cpp hal/cstd_div.c
    hal/clsn_vtable.cpp hal/bg_ctor_bridge.cpp hal/actor_vtables.cpp hal/actor_ctor_bridge.cpp hal/anim_bridge.cpp hal/anim_ctor_bridge.cpp
    hal/cxxname_bridge.cpp hal/megachar_stub.cpp hal/softfloat_abi.cpp unmatched/actor_earthquake.cpp unmatched/actor_spawn.cpp unmatched/actor_process_tables.cpp
    ${ACTOR_GAME_SOURCES}
    ${GATE4A_GEN} ${GATE4B_GEN} ${GATE8_GEN} ${GATE9_GEN} "${ROMDATA_C}")
if(MSVC)
    set_source_files_properties("${SM64DS_DECOMP_DIR}/src/func_0200d8c8.c"
        PROPERTIES COMPILE_OPTIONS "/FI${CMAKE_CURRENT_SOURCE_DIR}/hal/cstd_div.h")
else()
    set_source_files_properties("${SM64DS_DECOMP_DIR}/src/func_0200d8c8.c"
        PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/hal/cstd_div.h")
endif()
target_include_directories(smoke_actor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SM64DS_DECOMP_DIR}/include")
target_link_libraries(smoke_actor PRIVATE ntr)
target_compile_definitions(smoke_actor PRIVATE SM64DS_PLATFORM_PC
    PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")
if(MSVC)
    target_compile_options(smoke_actor PRIVATE /wd4311 /wd4312 /wd4068)
endif()

# Shared native fader objects retain constructor/vtable semantics.
set(FADER_NATIVE_SOURCES unmatched/fader_globals.cpp unmatched/fader_registers.cpp hal/half_transfer.cpp
    "${SM64DS_DECOMP_DIR}/src/_Z14ApproachLinearRiii.cpp")
list(APPEND FADER_NATIVE_SOURCES "${SM64DS_DECOMP_DIR}/src/engine/fader/_ZN10FaderColorD1Ev.cpp")
list(APPEND FADER_NATIVE_SOURCES "${SM64DS_DECOMP_DIR}/src/engine/fader/_ZN15FaderBrightness10SetToStartEv.cpp")
list(APPEND FADER_NATIVE_SOURCES "${SM64DS_DECOMP_DIR}/src/engine/fader/_ZN15FaderBrightness14SetForwardTimeEj.cpp")
list(APPEND FADER_NATIVE_SOURCES "${SM64DS_DECOMP_DIR}/src/engine/fader/_ZN15FaderBrightness15SetBackwardTimeEj.cpp")
list(APPEND FADER_NATIVE_SOURCES "${SM64DS_DECOMP_DIR}/src/engine/fader/_ZN15FaderBrightness20IsBetweenStartAndEndEv.cpp")
list(APPEND FADER_NATIVE_SOURCES "${SM64DS_DECOMP_DIR}/src/engine/fader/_ZN15FaderBrightness7IsAtEndEv.cpp")
list(APPEND FADER_NATIVE_SOURCES "${SM64DS_DECOMP_DIR}/src/engine/fader/_ZN15FaderBrightness8SetToEndEv.cpp")
list(APPEND FADER_NATIVE_SOURCES "${SM64DS_DECOMP_DIR}/src/engine/fader/_ZN15FaderBrightness9IsAtStartEv.cpp")
list(APPEND FADER_NATIVE_SOURCES "${SM64DS_DECOMP_DIR}/src/engine/fader/_ZN15FaderBrightnessD1Ev.cpp")
list(APPEND FADER_NATIVE_SOURCES "${SM64DS_DECOMP_DIR}/src/engine/fader/_ZN5Fader13AdvanceInterpEv.cpp")
list(APPEND FADER_NATIVE_SOURCES "${SM64DS_DECOMP_DIR}/src/engine/fader/_ZN5FaderD1Ev.cpp")
sm64ds_hostgen_symbols(FADER_MMIO_SOURCES
    _ZN10FaderColor11AdvanceFadeEv _ZN15FaderBrightness11AdvanceFadeEv
    _ZN2GX10LoadBGPlttEPKvjj _ZN3GXS10LoadBGPlttEPKvjj)
list(APPEND FADER_NATIVE_SOURCES ${FADER_MMIO_SOURCES})
get_target_property(FADER_BASE_SOURCES smoke_actor SOURCES)
list(REMOVE_ITEM FADER_BASE_SOURCES tests/smoke_actor.cpp)
add_executable(smoke_fader tests/smoke_fader.cpp ${FADER_BASE_SOURCES} ${FADER_NATIVE_SOURCES})
target_include_directories(smoke_fader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
target_link_libraries(smoke_fader PRIVATE ntr)
target_compile_definitions(smoke_fader PRIVATE SM64DS_PLATFORM_PC PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")

# Player integration reuses the actual actor engine, with its own unresolved
# closure kept separate so the validated actor gate remains runnable.
get_target_property(PLAYER_BASE_SOURCES smoke_actor SOURCES)
list(REMOVE_ITEM PLAYER_BASE_SOURCES tests/smoke_actor.cpp)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/slice_gate_player.txt" PLAYER_LINES)
sm64ds_hostgen_symbols(PLAYER_MMIO_GEN LenVec3 __sinit_ov002_0210804c func_0203d974 _ZN8daStar_c6RenderEv _Z26LoadOrUnloadObjectOverlaysPFviEi func_02019ac4 func_0204c24c func_0204c194 func_0204c0a8 func_0204c0e8 _ZN3OAM4LoadEv func_0204af3c func_0204b244 func_0204be40 func_0204b81c func_0204b028 func_0204bbd8 func_0204b460 func_02019144 func_02055454 func_02049ee8 _ZN3G2x12SetBGyAffineEPVtP9Matrix2x2iiii _ZN5Stage13InitResourcesEv _ZN5Stage16CleanupResourcesEv func_0202f58c func_0202f2c4 func_0202f3a4 _ZN7dWipe_c15SetBackwardTimeEj _ZN7dWipe_c14SetForwardTimeEj _ZN7dWipe_c11AdvanceFadeEv func_0202fb30 func_02055464 func_02055490 DisableVramBanks _ZN2G212GetBG2ScrPtrEv _ZN2G213GetBG2CharPtrEv _ZN3G3X11SetFogTableEPv _ZN3G3X13SetClearColorEtiiib _ZN3G3X6SetFogEbiii _ZN7Message12UpdateWindowEv func_020190b8 func_020321fc func_020554bc func_020556d0 Enable3dEngines Initialise3dGraphics _ZN2GX12SetBankForBGEt _ZN2GX13SetBankForOBJEt _ZN2GX13SetBankForTexEt _ZN2GX15SetBankForSubBGEt _ZN2GX15SetGraphicsModeEiii _ZN2GX16SetBankForSubOBJEt _ZN2GX17SetBankForTexPlttEt _ZN2GX6DispOnEv _ZN5Stage10PS_CleanupEv _ZN5Stage14LoadGraphics2DEbi _ZN5Stage7PS_InitEv _ZN5Stage7VE_InitEv _ZN5Stage9LC_UpdateEv _ZN5Stage9PS_UpdateEv _ZN5Stage9RenderFogEv _ZN5Stage9VE_UpdateEv _ZN5dCc_c7ProcessEv func_020345b0 func_02053be0 func_02053c10 func_02053ee0 func_02053fbc func_02053fe0 func_0205583c _ZN7Message13DisplaySavingEt daMip_c_classInit func_ov002_020b18f0 da1up_c_classInit_ONEUPKINOKO func_ov102_0214ae1c _ZN4cstd11ldiv_resultEv _ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3 _ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3 _ZN6Camera6RenderEv _ZN4View6RenderEv _ZN9FaderWipe11AdvanceFadeEv func_ov002_020e6ad4 _ZN4cstd3divEii func_02053380 _ZN3G2S12GetBG0ScrPtrEv func_ov002_020f5fe4
    _ZN2GX11LoadOBJPlttEPKvjj _ZN2GX22SetBankForSubBGExtPlttEt _ZN3G2S13GetBG3CharPtrEv _ZN3GXS11LoadOBJPlttEPKvjj _ZN3GXS13LoadBGExtPlttEPKvjj _ZN3GXS15SetGraphicsModeEi _ZN3GXS16EndLoadBGExtPlttEv _ZN3GXS18BeginLoadBGExtPlttEv
    Vram__Map func_02054118 _ZN3G2S12GetBG2ScrPtrEv _ZN3G2S12GetBG3ScrPtrEv
    _ZN8dScene_c14StartSceneFadeEjjt func_020197b8 func_0201ffcc)
set(PLAYER_EXTRA_SOURCES "")
foreach(line IN LISTS PLAYER_LINES)
    if(NOT line MATCHES "^#" AND NOT line STREQUAL "")
        sm64ds_manifest_source(line)
        list(APPEND PLAYER_EXTRA_SOURCES "${line}")
    endif()
endforeach()
sm64ds_honor_cpp_markers(PLAYER_EXTRA_SOURCES)
if(NOT MSVC)
    set_source_files_properties("${SM64DS_DECOMP_DIR}/src/__sinit_ov002_021019d0.c"
        "${SM64DS_DECOMP_DIR}/src/func_ov002_020e2c84.c"
        PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/hal/player_init_decls.h")
endif()

set(PLAYER_ALL_SOURCES ${FADER_NATIVE_SOURCES} ${PLAYER_BASE_SOURCES} ${PLAYER_EXTRA_SOURCES} ${PLAYER_MMIO_GEN})
# Complete original red-coin helpers retained by the common ov2 profile batch.
list(APPEND PLAYER_ALL_SOURCES ${SM64DS_DECOMP_DIR}/src/NumRedCoins.c ${SM64DS_DECOMP_DIR}/src/GiveRedCoins.c)
list(REMOVE_ITEM PLAYER_ALL_SOURCES "${SM64DS_DECOMP_DIR}/src/LenVec3.cpp" "${SM64DS_DECOMP_DIR}/src/LenVec3.c")
list(REMOVE_DUPLICATES PLAYER_ALL_SOURCES)
list(REMOVE_ITEM PLAYER_ALL_SOURCES "${SM64DS_DECOMP_DIR}/src/actors/daStar_c.cpp" "${SM64DS_DECOMP_DIR}/src/func_0203d974.c")
list(REMOVE_ITEM PLAYER_ALL_SOURCES "${SM64DS_DECOMP_DIR}/src/_Z26LoadOrUnloadObjectOverlaysPFviEi.cpp"
    "${SM64DS_DECOMP_DIR}/src/actors/dScene_c.cpp"
    "${SM64DS_DECOMP_DIR}/src/_ZN5Stage16CleanupResourcesEv.cpp")
sm64ds_drop_gcc_variant_twins(PLAYER_ALL_SOURCES)
if(NOT MSVC)
    # Player D1 emits D0/D1/D2 under GCC; the DS D0 file repeats the same body.
    list(REMOVE_ITEM PLAYER_ALL_SOURCES "${SM64DS_DECOMP_DIR}/src/_ZN6PlayerD0Ev.cpp")
endif()
if(NINTENDO_3DS)
    # The matched clear setter reads int through a one-byte bool local. Keep
    # retail register writes without the native stack overread.
    list(FILTER PLAYER_ALL_SOURCES EXCLUDE REGEX "/_ZN3G3X13SetClearColorEtiiib\\.(c|cpp)$")
    list(APPEND PLAYER_ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/unmatched/g3x_clear_color.cpp")
    # Retail camera reads an unspecified stack pointer in GoBehindPlayer.
    list(REMOVE_ITEM PLAYER_ALL_SOURCES "${SM64DS_DECOMP_DIR}/src/_ZN6Camera14GoBehindPlayerEj.cpp")
    list(APPEND PLAYER_ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/unmatched/camera_go_behind.cpp")
    # DS IPC bit-packing truncates native3DS pointers; preserve full pointers at
    # the function boundary and route channel7 to the synchronous sound service.
    list(FILTER PLAYER_ALL_SOURCES EXCLUDE REGEX "/IPCSend\\.(c|cpp)$")
    list(REMOVE_ITEM PLAYER_ALL_SOURCES "${SM64DS_DECOMP_DIR}/src/func_02051634.c")
    list(APPEND PLAYER_ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/audio/sound_archive.cpp")
    # Generated arrow TU provides defined GCC returns and native resource data.
    list(REMOVE_ITEM PLAYER_ALL_SOURCES "${SM64DS_DECOMP_DIR}/src/actors/daObjYajirusi_c.cpp")
endif()
add_executable(smoke_player tests/smoke_player.cpp platform/3ds/player_input.cpp hal/system_lifecycle.cpp hal/native_clock.cpp hal/alarm_dispatch.cpp unmatched/alarm_queue.cpp unmatched/alarm_rearm.cpp ${PLAYER_ALL_SOURCES} unmatched/player_init.cpp unmatched/player_vtable.cpp unmatched/player_hold_light.cpp unmatched/player_floor_transition.cpp unmatched/player_script.cpp)
if(NINTENDO_3DS)
    # Strong SDK upload entry points override the weak flat-window model HAL
    # only in the complete native target, with a runtime physical-bus guard.
    target_sources(smoke_player PRIVATE platform/3ds/gx_upload_physical.cpp)
    # Retail CPU fills/copies must publish through the physical bank mapping.
    # The wrappers retain the original routines when that bus is disabled.
    target_sources(smoke_player PRIVATE platform/3ds/vram_store16.cpp)
    target_link_options(smoke_player PRIVATE
        "-Wl,--wrap=MultiStore16"
        "-Wl,--wrap=MultiCopyHalf"
        "-Wl,--wrap=MultiStore32Bytes")
endif()
if(NINTENDO_3DS)
    set(DOOR_DATA "${CMAKE_CURRENT_BINARY_DIR}/door_data.cpp")
    add_custom_command(OUTPUT "${DOOR_DATA}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/door_data.py"
            "${SM64DS_DECOMP_DIR}" "${DOOR_DATA}"
        DEPENDS tools/door_data.py
            "${SM64DS_DECOMP_DIR}/src/__sinit_ov100_02147698.c"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov100/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov100/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0100.bin")
    target_sources(smoke_player PRIVATE "${DOOR_DATA}")
    set(SIGNPOST_PMFS "${CMAKE_CURRENT_BINARY_DIR}/signpost_pmfs.cpp")
    add_custom_command(OUTPUT "${SIGNPOST_PMFS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/actor_pmfs.py"
            "${SM64DS_DECOMP_DIR}" 2 0x020ad660
            src/__sinit_ov002_02101738.c "${SIGNPOST_PMFS}"
        DEPENDS tools/actor_pmfs.py
            "${SM64DS_DECOMP_DIR}/src/__sinit_ov002_02101738.c"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin")
    target_sources(smoke_player PRIVATE "${SIGNPOST_PMFS}" unmatched/signpost_factory.c)
    set(SIGNPOST_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/signpost_native.cpp")
    add_custom_command(OUTPUT "${SIGNPOST_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/signpost_native.py"
            "${SM64DS_DECOMP_DIR}/src/actors/daObjTatefuda_c.cpp" "${SIGNPOST_NATIVE}"
        DEPENDS tools/signpost_native.py "${SM64DS_DECOMP_DIR}/src/actors/daObjTatefuda_c.cpp")
    target_sources(smoke_player PRIVATE "${SIGNPOST_NATIVE}")
    set(HATENA_PMFS "${CMAKE_CURRENT_BINARY_DIR}/hatena_pmfs.cpp")
    add_custom_command(OUTPUT "${HATENA_PMFS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/actor_pmfs.py"
            "${SM64DS_DECOMP_DIR}" 102 0x02148a80
            src/__sinit_ov102_0214d908.c "${HATENA_PMFS}"
        DEPENDS tools/actor_pmfs.py
            "${SM64DS_DECOMP_DIR}/src/__sinit_ov102_0214d908.c"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov102/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0102.bin")
    target_sources(smoke_player PRIVATE "${HATENA_PMFS}")
    set(LEVEL_LOADER_DATA "${CMAKE_CURRENT_BINARY_DIR}/level_loader_data.cpp")
    set(ALL_LEVEL_DATA "${CMAKE_CURRENT_BINARY_DIR}/all_level_data.cpp")
    set(ALL_LEVEL_INPUTS "${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin")
    foreach(LEVEL_OVERLAY RANGE 8 59)
        string(LENGTH "${LEVEL_OVERLAY}" LEVEL_DIGITS)
        if(LEVEL_DIGITS EQUAL 1)
            set(LEVEL_PAD "00${LEVEL_OVERLAY}")
        else()
            set(LEVEL_PAD "0${LEVEL_OVERLAY}")
        endif()
        list(APPEND ALL_LEVEL_INPUTS
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov${LEVEL_PAD}/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov${LEVEL_PAD}/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0${LEVEL_PAD}.bin")
    endforeach()
    add_custom_command(OUTPUT "${ALL_LEVEL_DATA}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/all_level_data.py"
            "${SM64DS_DECOMP_DIR}" "${ALL_LEVEL_DATA}"
        DEPENDS tools/all_level_data.py tools/native_actor_overlay_manifest.json tools/castle_level_data.py ${ALL_LEVEL_INPUTS})
    target_sources(smoke_player PRIVATE "${ALL_LEVEL_DATA}" unmatched/level_data_reset.cpp unmatched/overlay_destructors.cpp)
    set(SM64DS_SOURCE_ROM "" CACHE FILEPATH "Local SM64DS ROM for native overlay metadata")
    if(NOT EXISTS "${SM64DS_SOURCE_ROM}")
        message(FATAL_ERROR "Set SM64DS_SOURCE_ROM to the local SM64DS ROM")
    endif()
    set(NATIVE_OVERLAY_INFO "${CMAKE_CURRENT_BINARY_DIR}/native_overlay_info.cpp")
    file(GLOB NATIVE_OVERLAY_SYMBOLS "${SM64DS_DECOMP_DIR}/config/arm9/overlays/*/symbols.txt")
    file(GLOB NATIVE_OVERLAY_BLOBS "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_*.bin")
    add_custom_command(OUTPUT "${NATIVE_OVERLAY_INFO}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/native_overlay_info.py"
            "${SM64DS_DECOMP_DIR}" "${SM64DS_SOURCE_ROM}" "${NATIVE_OVERLAY_INFO}"
        DEPENDS tools/native_overlay_info.py tools/native_actor_lifecycle.py tools/native_actor_overlay_manifest.json tools/minigame_imports.py menu/minigame_imports.inc
            tools/all_level_data.py tools/castle_level_data.py
            "${SM64DS_SOURCE_ROM}" ${NATIVE_OVERLAY_SYMBOLS} ${NATIVE_OVERLAY_BLOBS} ${ALL_LEVEL_INPUTS}
        VERBATIM)
    target_sources(smoke_player PRIVATE "${NATIVE_OVERLAY_INFO}" unmatched/native_overlay.cpp)

    set(STAGE_TABLES "${CMAKE_CURRENT_BINARY_DIR}/stage_tables.cpp")
    add_custom_command(OUTPUT "${STAGE_TABLES}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/stage_tables.py"
            "${SM64DS_DECOMP_DIR}" "${STAGE_TABLES}"
        DEPENDS tools/stage_tables.py
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin")
    target_sources(smoke_player PRIVATE "${STAGE_TABLES}")
    set(GLYPH_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/glyph_native.cpp")
    add_custom_command(OUTPUT "${GLYPH_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/glyph_native.py"
            "${SM64DS_DECOMP_DIR}/src/func_020316d8.c" "${GLYPH_NATIVE}"
        DEPENDS tools/glyph_native.py "${SM64DS_DECOMP_DIR}/src/func_020316d8.c")
    target_sources(smoke_player PRIVATE "${GLYPH_NATIVE}")
    set(STAGE_STATE_BSS "${CMAKE_CURRENT_BINARY_DIR}/stage_state_bss.cpp")
    add_custom_command(OUTPUT "${STAGE_STATE_BSS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/stage_state_bss.py"
            "${SM64DS_DECOMP_DIR}/config/arm9" "${STAGE_STATE_BSS}"
        DEPENDS tools/stage_state_bss.py "${SM64DS_DECOMP_DIR}/config/arm9/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov001/symbols.txt")
    target_sources(smoke_player PRIVATE "${STAGE_STATE_BSS}" unmatched/particle_inherited_callbacks.cpp unmatched/particle_rotation.cpp unmatched/screen_wipe_global.cpp unmatched/player_pause_bridge.cpp hal/save_cart.cpp hal/thread_wait.cpp platform/3ds/backlight.cpp platform/3ds/lcd_power.cpp)
    set(OAM_TABLES "${CMAKE_CURRENT_BINARY_DIR}/oam_tables.cpp")
    add_custom_command(OUTPUT "${OAM_TABLES}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/oam_tables.py"
            "${SM64DS_DECOMP_DIR}" "${OAM_TABLES}"
        DEPENDS tools/oam_tables.py
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov001/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov001/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0001.bin"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin")
    target_sources(smoke_player PRIVATE "${OAM_TABLES}")
    add_custom_command(OUTPUT "${LEVEL_LOADER_DATA}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/level_loader_data.py"
            "${SM64DS_DECOMP_DIR}" "${LEVEL_LOADER_DATA}"
        DEPENDS tools/level_loader_data.py
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin")
    target_sources(smoke_player PRIVATE "${LEVEL_LOADER_DATA}" unmatched/level_entrance_fader.cpp unmatched/camera_factory.cpp unmatched/bird_takeoff.c unmatched/butterfly_data.cpp unmatched/cannon_data.cpp)
    set(CASTLE_LEVEL_DATA "${CMAKE_CURRENT_BINARY_DIR}/castle_level_data.cpp")
    add_custom_command(OUTPUT "${CASTLE_LEVEL_DATA}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/castle_level_data.py"
            "${SM64DS_DECOMP_DIR}" "${CASTLE_LEVEL_DATA}"
        DEPENDS tools/castle_level_data.py
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov009/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov009/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0009.bin")
    target_sources(smoke_player PRIVATE "${CASTLE_LEVEL_DATA}" unmatched/castle_overlay.cpp)
    set(PLAYER_OVERLAY_CONFIG "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002")
    set(CAMERA_CONSTANTS "${CMAKE_CURRENT_BINARY_DIR}/camera_constants.cpp")
    add_custom_command(OUTPUT "${CAMERA_CONSTANTS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/camera_constants.py"
            "${SM64DS_DECOMP_DIR}" "${CAMERA_CONSTANTS}"
        DEPENDS tools/camera_constants.py "${SM64DS_DECOMP_DIR}/config/arm9/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${CAMERA_CONSTANTS}")
    set(CAMERA_CALLBACKS "${CMAKE_CURRENT_BINARY_DIR}/camera_callbacks.cpp")
    add_custom_command(OUTPUT "${CAMERA_CALLBACKS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/camera_callbacks.py"
            "${SM64DS_DECOMP_DIR}" "${CAMERA_CALLBACKS}"
        DEPENDS tools/camera_callbacks.py "${SM64DS_DECOMP_DIR}/config/arm9/symbols.txt"
            "${SM64DS_DECOMP_DIR}/src/__sinit_02073a24.c"
            "${SM64DS_DECOMP_DIR}/src/func_02008550.cpp"
            "${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${CAMERA_CALLBACKS}")
    set(LEVEL_SCRIPTS "${CMAKE_CURRENT_BINARY_DIR}/level_scripts.cpp")
    add_custom_command(OUTPUT "${LEVEL_SCRIPTS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/level_scripts.py"
            "${SM64DS_DECOMP_DIR}" "${LEVEL_SCRIPTS}"
        DEPENDS tools/level_scripts.py "${SM64DS_DECOMP_DIR}/config/arm9/symbols.txt"
            "${SM64DS_DECOMP_DIR}/src/__sinit_02073e6c.c"
            "${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${LEVEL_SCRIPTS}")
    set(KEY_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/key_resources.cpp")
    add_custom_command(OUTPUT "${KEY_RESOURCES}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/key_resources.py"
            "${SM64DS_DECOMP_DIR}" "${KEY_RESOURCES}"
        DEPENDS tools/key_resources.py "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov089/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0089.bin"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${KEY_RESOURCES}")
    set(SHADOW_DATA "${CMAKE_CURRENT_BINARY_DIR}/shadow_data.cpp")
    add_custom_command(OUTPUT "${SHADOW_DATA}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/shadow_data.py"
            "${SM64DS_DECOMP_DIR}" "${SHADOW_DATA}"
        DEPENDS tools/shadow_data.py "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov001/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov001/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0001.bin"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${SHADOW_DATA}")
    set(ARCHIVE_DATA "${CMAKE_CURRENT_BINARY_DIR}/archive_data.cpp")
    add_custom_command(OUTPUT "${ARCHIVE_DATA}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/archive_data.py"
            "${SM64DS_DECOMP_DIR}" "${ARCHIVE_DATA}"
        DEPENDS tools/archive_data.py "${SM64DS_DECOMP_DIR}/config/arm9/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${ARCHIVE_DATA}")
    set(MESSAGE_DATA "${CMAKE_CURRENT_BINARY_DIR}/message_data.cpp")
    add_custom_command(OUTPUT "${MESSAGE_DATA}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/message_data.py"
            "${SM64DS_DECOMP_DIR}" "${MESSAGE_DATA}"
        DEPENDS tools/message_data.py "${SM64DS_DECOMP_DIR}/config/arm9/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${MESSAGE_DATA}")
    set(PLATFORM_BSS "${CMAKE_CURRENT_BINARY_DIR}/platform_bss.cpp")
    add_custom_command(OUTPUT "${PLATFORM_BSS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/platform_bss.py"
            "${SM64DS_DECOMP_DIR}/config/arm9" "${PLATFORM_BSS}"
        DEPENDS tools/platform_bss.py "${SM64DS_DECOMP_DIR}/config/arm9/symbols.txt"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${PLATFORM_BSS}")
    set(SCENE_BSS "${CMAKE_CURRENT_BINARY_DIR}/scene_bss.cpp")
    add_custom_command(OUTPUT "${SCENE_BSS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/scene_bss.py"
            "${SM64DS_DECOMP_DIR}/config/arm9" "${SCENE_BSS}"
        DEPENDS tools/scene_bss.py "${SM64DS_DECOMP_DIR}/config/arm9/symbols.txt"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${SCENE_BSS}")
    set(MESSAGE_BSS "${CMAKE_CURRENT_BINARY_DIR}/message_bss.cpp")
    add_custom_command(OUTPUT "${MESSAGE_BSS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/message_bss.py"
            "${SM64DS_DECOMP_DIR}/config/arm9" "${MESSAGE_BSS}"
        DEPENDS tools/message_bss.py "${SM64DS_DECOMP_DIR}/config/arm9/symbols.txt"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${MESSAGE_BSS}")
    set(PLAYER_OVERLAY_BSS "${CMAKE_CURRENT_BINARY_DIR}/player_overlay_bss.cpp")
    add_custom_command(OUTPUT "${PLAYER_OVERLAY_BSS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay_bss.py"
            "${PLAYER_OVERLAY_CONFIG}" "${PLAYER_OVERLAY_BSS}"
        DEPENDS tools/overlay_bss.py "${PLAYER_OVERLAY_CONFIG}/symbols.txt"
            "${PLAYER_OVERLAY_CONFIG}/delinks.txt"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${PLAYER_OVERLAY_BSS}" unmatched/player_globals.cpp)
    set(PLAYER_CALLBACKS "${CMAKE_CURRENT_BINARY_DIR}/player_callbacks.cpp")
    add_custom_command(OUTPUT "${PLAYER_CALLBACKS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/player_callbacks.py"
            "${SM64DS_DECOMP_DIR}" "${PLAYER_CALLBACKS}"
        DEPENDS tools/player_callbacks.py "${PLAYER_OVERLAY_CONFIG}/symbols.txt"
            "${SM64DS_DECOMP_DIR}/src/__sinit_ov002_021019d0.c"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${PLAYER_CALLBACKS}")
    set(SCRIPT_CALLBACKS "${CMAKE_CURRENT_BINARY_DIR}/script_callbacks.cpp")
    add_custom_command(OUTPUT "${SCRIPT_CALLBACKS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/script_callbacks.py"
            "${SM64DS_DECOMP_DIR}" "${SCRIPT_CALLBACKS}"
        DEPENDS tools/script_callbacks.py "${PLAYER_OVERLAY_CONFIG}/symbols.txt"
            "${SM64DS_DECOMP_DIR}/src/func_ov002_020f7d74.cpp"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${SCRIPT_CALLBACKS}")
    set(PLAYER_RODATA "${CMAKE_CURRENT_BINARY_DIR}/player_rodata.cpp")
    add_custom_command(OUTPUT "${PLAYER_RODATA}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay_rodata.py"
            "${SM64DS_DECOMP_DIR}" "${PLAYER_RODATA}"
        DEPENDS tools/overlay_rodata.py "${PLAYER_OVERLAY_CONFIG}/symbols.txt"
            "${PLAYER_OVERLAY_CONFIG}/relocs.txt" "${PLAYER_OVERLAY_CONFIG}/delinks.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${PLAYER_RODATA}")
    set(PLAYER_CONSTANTS "${CMAKE_CURRENT_BINARY_DIR}/player_constants.cpp")
    add_custom_command(OUTPUT "${PLAYER_CONSTANTS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/player_constants.py"
            "${SM64DS_DECOMP_DIR}" "${PLAYER_CONSTANTS}"
        DEPENDS tools/player_constants.py "${PLAYER_OVERLAY_CONFIG}/symbols.txt"
            "${PLAYER_OVERLAY_CONFIG}/relocs.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin"
        VERBATIM)
    target_sources(smoke_player PRIVATE "${PLAYER_CONSTANTS}")



endif()
target_include_directories(smoke_player PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
target_link_libraries(smoke_player PRIVATE ntr)
target_compile_definitions(smoke_player PRIVATE SM64DS_PLATFORM_PC SM64DS_NATIVE_SHADOW
    PORT_REPO_ROOT="${PORT_REPO_ROOT_ABS}")

sm64ds_hostgen_symbols(PARTICLE_MATH_GEN func_02053380)
add_executable(smoke_particle_math tests/smoke_particle_math.cpp ${PARTICLE_MATH_GEN})
target_include_directories(smoke_particle_math PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(smoke_particle_math PRIVATE ntr)

if(NINTENDO_3DS)
    add_executable(smoke_alarm_dispatch tests/smoke_alarm_dispatch.cpp hal/native_clock.cpp
        hal/alarm_dispatch.cpp unmatched/alarm_queue.cpp unmatched/alarm_rearm.cpp
        "${SM64DS_DECOMP_DIR}/src/func_020599e8.cpp"
        "${SM64DS_DECOMP_DIR}/src/func_02056e4c.c"
        "${SM64DS_DECOMP_DIR}/src/func_02059950.c")
    target_include_directories(smoke_alarm_dispatch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
    target_link_libraries(smoke_alarm_dispatch PRIVATE ntr)
    add_executable(smoke_alarm_queue tests/smoke_alarm_queue.cpp unmatched/alarm_queue.cpp
        "${SM64DS_DECOMP_DIR}/src/func_02059950.c")
    target_include_directories(smoke_alarm_queue PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
add_executable(smoke_clock tests/smoke_clock.cpp hal/native_clock.cpp)

add_executable(smoke_softfloat tests/smoke_softfloat.cpp hal/softfloat_abi.cpp)
target_link_libraries(smoke_softfloat PRIVATE ntr)

set(PORT_ALL_SMOKES smoke smoke_heap smoke_roots smoke_gx smoke_model smoke_frames
    smoke_soak smoke_clsn smoke_modelanim smoke_oam smoke_soak_anim smoke_anim
    smoke_fs smoke_actor smoke_player smoke_softfloat smoke_fader smoke_particle_math smoke_clock)

# 3DS: every smoke runner becomes a .3dsx. The runner's own main is renamed to
# smoke_main and platform/3ds/main3ds.c brings up the console/sdmc log around it.
if(NINTENDO_3DS)
    add_executable(smoke_save_cart tests/smoke_save_cart.cpp hal/save_cart.cpp)
    target_include_directories(smoke_save_cart PRIVATE "${SM64DS_DECOMP_DIR}/include")
    add_executable(smoke_backlight tests/smoke_backlight.cpp platform/3ds/backlight.cpp)
    add_executable(smoke_lcd_power tests/smoke_lcd_power.cpp platform/3ds/lcd_power.cpp)
    target_link_libraries(smoke_lcd_power PRIVATE ntr)
    add_executable(smoke_thread_wait tests/smoke_thread_wait.cpp hal/thread_wait.cpp
        "${SM64DS_DECOMP_DIR}/src/func_020587e4.c")
    target_include_directories(smoke_thread_wait PRIVATE "${SM64DS_DECOMP_DIR}/include")
    target_link_libraries(smoke_thread_wait PRIVATE ntr)
    sm64ds_hostgen_symbols(VBLANK_NATIVE_GEN _ZN3IRQ13VBlankHandlerEv)
    target_sources(smoke_player PRIVATE ${VBLANK_NATIVE_GEN})
    add_executable(smoke_vblank_irq tests/smoke_vblank_irq.cpp hal/thread_wait.cpp
        ${VBLANK_NATIVE_GEN} "${SM64DS_DECOMP_DIR}/src/func_0201a4e4.c")
    target_include_directories(smoke_vblank_irq PRIVATE "${SM64DS_DECOMP_DIR}/include")
    target_link_libraries(smoke_vblank_irq PRIVATE ntr)
    add_executable(smoke_overlay_destructors tests/smoke_overlay_destructors.cpp
        unmatched/overlay_destructors.cpp)
    target_link_libraries(smoke_overlay_destructors PRIVATE ntr)
    list(APPEND PORT_ALL_SMOKES smoke_overlay_destructors)
    add_executable(smoke_level_reset tests/smoke_level_reset.cpp unmatched/level_data_reset.cpp
        "${ALL_LEVEL_DATA}" "${CASTLE_LEVEL_DATA}")
    target_link_libraries(smoke_level_reset PRIVATE ntr)
    list(APPEND PORT_ALL_SMOKES smoke_level_reset)
    add_executable(smoke_oam_reset tests/smoke_oam_reset.cpp hal/model_host.cpp
        "${STAGE_STATE_BSS}" "${SM64DS_DECOMP_DIR}/src/_ZN3OAM5ResetEv.cpp")
    target_include_directories(smoke_oam_reset PRIVATE "${SM64DS_DECOMP_DIR}/include")
    target_link_libraries(smoke_oam_reset PRIVATE ntr)
    list(APPEND PORT_ALL_SMOKES smoke_oam_reset)
    list(APPEND PORT_ALL_SMOKES smoke_alarm_queue smoke_alarm_dispatch smoke_save_cart smoke_backlight smoke_lcd_power smoke_thread_wait smoke_vblank_irq)
    foreach(t IN LISTS PORT_ALL_SMOKES)
        target_sources(${t} PRIVATE platform/3ds/main3ds.cpp)
        target_compile_definitions(${t} PRIVATE main=smoke_main)
        target_compile_options(${t} PRIVATE -fno-strict-aliasing -Wno-narrowing $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
        target_link_options(${t} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/aliases.ld")
        ctr_create_3dsx(${t})
    endforeach()
    target_compile_options(ntr PRIVATE -fno-strict-aliasing)

    # The live viewer: smoke_frames' game loop, blitted to the top screen.
    # BTP texture-pattern playback (TextureSequence): the game's own decode.
    set(VIEWER_BTP_SOURCES
        ${SM64DS_DECOMP_DIR}/src/func_02046d50.c ${SM64DS_DECOMP_DIR}/src/func_02046bbc.c
        ${SM64DS_DECOMP_DIR}/src/func_020471ac.c ${SM64DS_DECOMP_DIR}/src/func_02047144.c ${SM64DS_DECOMP_DIR}/src/func_02046590.c
        ${SM64DS_DECOMP_DIR}/src/_ZN15TextureSequence8LoadFileER13SharedFilePtr.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN15TextureSequence17UpdateFileOffsetsER8BTP_File.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN4cstd6strcmpEPKcS1_.cpp)
    sm64ds_honor_cpp_markers(VIEWER_BTP_SOURCES)
    add_executable(sm64ds_viewer platform/3ds/viewer.cpp platform/3ds/viewer_3ds.cpp
        platform/3ds/viewer_clsn.cpp ${VIEWER_BTP_SOURCES}
        hal/cstd_div.c hal/clsn_vtable.cpp hal/bg_ctor_bridge.cpp ${SLICE8_SOURCES} ${GATE8_GEN}
        hal/heap_globals.cpp hal/os_arena.cpp hal/heap_vtable.cpp hal/fs.cpp
        hal/model_host.cpp hal/gx_upload_bridge.cpp hal/ctor_bridge.cpp
        ${SLICE2_SOURCES} ${SLICE3A_SOURCES} ${SLICE3B_SOURCES} ${SLICE4B_SOURCES}
        ${GATE4A_GEN} ${GATE4B_GEN} "${ROMDATA_C}")
    target_include_directories(sm64ds_viewer PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
    ctr_add_shader_library(viewer_shaders platform/3ds/vshader.v.pica)
    dkp_add_embedded_binary_library(viewer_shaders_bin viewer_shaders)
    target_link_libraries(sm64ds_viewer PRIVATE ntr viewer_shaders_bin citro3d)
    target_sources(smoke_player PRIVATE platform/3ds/viewer_3ds.cpp platform/3ds/player_present.cpp platform/3ds/runtime_wrappers.cpp
        platform/3ds/observed_cadence.cpp)
    # analog-pad: optional analog Circle Pad on the original touch-stick path
    # (settings.ini [input] circle_pad, SM64DS_ANALOG_PAD). The wrappers call
    # the originals first; dMap_c only is shown touching=0 for the analog sample.
    target_sources(smoke_player PRIVATE platform/3ds/analog_pad.cpp platform/3ds/enhance_settings.cpp)
    # cstick-camera: New 3DS C-Stick camera (settings.ini [input] camera_cstick,
    # SM64DS_CSTICK_CAMERA): DS-arrow commands after Stage::CheckInput (same
    # wrapper) + analog hooks in func_02009e70 / func_0200bb28 (identity when off).
    target_sources(smoke_player PRIVATE platform/3ds/cstick_camera.cpp)
    # visual-enhancements phase 2: SELECT+START settings screen (input filter,
    # overlay model/renderer), HD title logo (on-device 4xBR from the game's own
    # BG layer, cached on SD), presentation options. enhance_settings.cpp above.
    foreach(enhance_target IN ITEMS smoke_player sm64ds_viewer)
        target_sources(${enhance_target} PRIVATE platform/3ds/enhance_runtime.cpp platform/3ds/enhance_overlay.cpp
            platform/3ds/enhance_logo.cpp platform/3ds/enhance_upscale.cpp)
    endforeach()
    target_sources(sm64ds_viewer PRIVATE platform/3ds/enhance_settings.cpp)
    target_link_options(smoke_player PRIVATE
        "-Wl,--wrap=_ZN5Stage10CheckInputEv"
        "-Wl,--wrap=_ZN7fBase_c14BeforeBehaviorEv"
        "-Wl,--wrap=_ZN7fBase_c13AfterBehaviorEj")
    # Only the game executable supplies the original SDK transfer entry point.
    target_sources(smoke_player PRIVATE platform/3ds/bmd_sdk_entry.cpp)
    target_link_options(smoke_player PRIVATE "-Wl,--wrap=func_01ffde98")
    # Both renderers may drain completed GPU commands before reusing storage.
    foreach(command_budget_target IN ITEMS smoke_player sm64ds_viewer)
        target_sources(${command_budget_target} PRIVATE platform/3ds/command_drain.cpp)
        target_link_options(${command_budget_target} PRIVATE
            "-Wl,--wrap=GX_BindQueue"
            "-Wl,--wrap=C3D_DrawArrays"
            "-Wl,--wrap=C3D_DrawElements"
            "-Wl,--wrap=C3D_FrameDrawOn"
            "-Wl,--wrap=C3D_TexBind"
            "-Wl,--wrap=GX_ProcessCommandList"
            "-Wl,--wrap=C3D_FrameBegin"
            "-Wl,--wrap=C3D_FrameEnd"
            "-Wl,--wrap=GPUCMD_Add"
            "-Wl,--wrap=GPUCMD_AddRawCommands")
    endforeach()
    # Mesh capture is opt-in at runtime (PLAYER_NATIVE_MESH); refusal uses CPU.
    # Camera wrappers retain original game implementations and observe LookAt.
    target_sources(smoke_player PRIVATE platform/3ds/native_mesh_camera.cpp)
    target_link_options(smoke_player PRIVATE
        "-Wl,--wrap=_ZN6Camera6RenderEv"
        "-Wl,--wrap=_ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3"
        "-Wl,--wrap=_ZN6Camera11ChangeStateEPNS_5StateE")
    target_compile_definitions(smoke_player PRIVATE PLAYER_SHARED_GFX)
    target_link_libraries(smoke_player PRIVATE viewer_shaders_bin citro3d)
    # Keep source flags stable when changing compositor backends. Makefiles
    # depend on a shared flags.make even for per-source COMPILE_DEFINITIONS;
    # toggling those definitions recompiles the complete original game slice.
    set(GPU_COMPOSITE_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/.dkp-generated/presentation")
    configure_file(platform/3ds/gpu_composite_build_config.h.in
        "${GPU_COMPOSITE_CONFIG_DIR}/gpu_composite_build_config.h")
    set_property(SOURCE platform/3ds/viewer_3ds.cpp platform/3ds/player_present.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS SM64DS_GPU_COMPOSITE_CONFIG)
    set_property(SOURCE platform/3ds/viewer_3ds.cpp platform/3ds/player_present.cpp
        APPEND PROPERTY INCLUDE_DIRECTORIES "${GPU_COMPOSITE_CONFIG_DIR}")
    set_property(SOURCE platform/3ds/viewer_3ds.cpp APPEND PROPERTY
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/.dkp-generated/gpu_composite_shaders_bin")
    if(SM64DS_EXPERIMENTAL_GPU_COMPOSITE)
        ctr_add_shader_library(gpu_composite_shaders platform/3ds/gpu_composite.v.pica)
        dkp_add_embedded_binary_library(gpu_composite_shaders_bin gpu_composite_shaders)
        add_library(sm64ds_gpu_composite STATIC platform/3ds/gpu_composite.cpp
            platform/3ds/native_obj_plan.cpp platform/3ds/native_obj_gpu.cpp
            platform/3ds/native_obj_merge.cpp platform/3ds/native_obj_input_views.cpp)
        target_include_directories(sm64ds_gpu_composite PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/ntr/include")
        target_link_libraries(sm64ds_gpu_composite PRIVATE citro3d ntr)
        # The embedded-binary object library publishes its generated include
        # directory. Linking it normally changes flags.make for every original
        # game source when this option toggles. Only the viewer includes it.
        foreach(gpu_viewer_target IN ITEMS smoke_player sm64ds_viewer)
            target_sources(${gpu_viewer_target} PRIVATE
                $<TARGET_OBJECTS:gpu_composite_shaders_bin>)
            add_dependencies(${gpu_viewer_target} gpu_composite_shaders_bin)
            target_link_libraries(${gpu_viewer_target} PRIVATE sm64ds_gpu_composite)
        endforeach()
    endif()
    target_compile_definitions(sm64ds_viewer PRIVATE SM64DS_PLATFORM_PC)
    target_compile_options(sm64ds_viewer PRIVATE -fno-strict-aliasing -Wno-narrowing
        $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
    target_link_options(sm64ds_viewer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/aliases.ld")
    ctr_generate_smdh(sm64ds_viewer.smdh NAME "SM64DS viewer"
        DESCRIPTION "Super Mario 64 DS decomp - live render" AUTHOR "sm64ds-decomp port")
    ctr_create_3dsx(sm64ds_viewer SMDH sm64ds_viewer.smdh)
endif()

if(BUILD_TESTING AND NOT NINTENDO_3DS)
    set(PORT_SMOKE_TARGETS
        smoke
        smoke_heap
        smoke_roots
        smoke_gx
        smoke_model
        smoke_frames
        smoke_soak
        smoke_clsn
        smoke_modelanim
        smoke_oam
        smoke_soak_anim
        smoke_anim
        smoke_fs
        smoke_actor)
    foreach(smoke_target IN LISTS PORT_SMOKE_TARGETS)
        add_test(NAME ${smoke_target} COMMAND $<TARGET_FILE:${smoke_target}>)
        set_tests_properties(${smoke_target} PROPERTIES
            WORKING_DIRECTORY "${PORT_REPO_ROOT_ABS}")
    endforeach()
endif()

if(NINTENDO_3DS)
    set(HATENA_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/hatena_native.cpp")
    add_custom_command(OUTPUT "${HATENA_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/hatena_native.py"
            "${SM64DS_DECOMP_DIR}/src/actors/daObjHatenaBlock_c.cpp" "${HATENA_NATIVE}"
        DEPENDS tools/hatena_native.py "${SM64DS_DECOMP_DIR}/src/actors/daObjHatenaBlock_c.cpp")
    target_sources(smoke_player PRIVATE "${HATENA_NATIVE}")
endif()

if(NINTENDO_3DS)
    set(ONEUP_PMFS "${CMAKE_CURRENT_BINARY_DIR}/oneup_pmfs.cpp")
    add_custom_command(OUTPUT "${ONEUP_PMFS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/actor_pmfs.py"
            "${SM64DS_DECOMP_DIR}" 2 0x020ad660
            src/__sinit_ov002_02100adc.c "${ONEUP_PMFS}"
        DEPENDS tools/actor_pmfs.py
            "${SM64DS_DECOMP_DIR}/src/__sinit_ov002_02100adc.c"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin")
    target_sources(smoke_player PRIVATE "${ONEUP_PMFS}")
endif()

if(NINTENDO_3DS)
    set(MIP_PMFS "${CMAKE_CURRENT_BINARY_DIR}/mip_pmfs.cpp")
    add_custom_command(OUTPUT "${MIP_PMFS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/actor_pmfs.py"
            "${SM64DS_DECOMP_DIR}" 85 0x02129020
            src/__sinit_ov085_0212f5ec.c "${MIP_PMFS}"
        DEPENDS tools/actor_pmfs.py
            "${SM64DS_DECOMP_DIR}/src/__sinit_ov085_0212f5ec.c"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov085/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0085.bin")
    target_sources(smoke_player PRIVATE "${MIP_PMFS}")
endif()

if(NINTENDO_3DS)
    set(JUGEM_PMFS "${CMAKE_CURRENT_BINARY_DIR}/jugem_pmfs.cpp")
    add_custom_command(OUTPUT "${JUGEM_PMFS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/actor_pmfs.py"
            "${SM64DS_DECOMP_DIR}" 85 0x02129020
            src/__sinit_ov085_0212fa40.c "${JUGEM_PMFS}"
        DEPENDS tools/actor_pmfs.py
            "${SM64DS_DECOMP_DIR}/src/__sinit_ov085_0212fa40.c"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov085/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0085.bin")
    target_sources(smoke_player PRIVATE "${JUGEM_PMFS}")
endif()

if(NINTENDO_3DS)
    set(BLOCK_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/block_native.cpp")
    add_custom_command(OUTPUT "${BLOCK_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/block_native.py"
            "${SM64DS_DECOMP_DIR}/src/actors/daObjBlockL_c.cpp" "${BLOCK_NATIVE}"
        DEPENDS tools/block_native.py "${SM64DS_DECOMP_DIR}/src/actors/daObjBlockL_c.cpp")
    target_sources(smoke_player PRIVATE "${BLOCK_NATIVE}")
endif()

if(NINTENDO_3DS)
    set(FISH_PMFS "${CMAKE_CURRENT_BINARY_DIR}/fish_pmfs.cpp")
    add_custom_command(OUTPUT "${FISH_PMFS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/actor_pmfs.py"
            "${SM64DS_DECOMP_DIR}" 100 0x02140d80
            src/__sinit_ov100_02147bc0.c "${FISH_PMFS}"

        DEPENDS tools/actor_pmfs.py
            "${SM64DS_DECOMP_DIR}/src/__sinit_ov100_02147bc0.c"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov100/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0100.bin")
    target_sources(smoke_player PRIVATE "${FISH_PMFS}")
    target_sources(smoke_player PRIVATE unmatched/fish_resources.cpp)
endif()

if(NINTENDO_3DS)
    target_sources(smoke_player PRIVATE unmatched/stage_factory.cpp)
endif()

if(NINTENDO_3DS)
    set(TOAD_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/toad_native.cpp")
    add_custom_command(OUTPUT "${TOAD_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/toad_native.py"
            "${SM64DS_DECOMP_DIR}" "${TOAD_NATIVE}"
        DEPENDS tools/toad_native.py "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0085.bin")
    target_sources(smoke_player PRIVATE "${TOAD_NATIVE}")
endif()

if(NINTENDO_3DS)
    set(PEACH_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/peach_native.cpp")
    add_custom_command(OUTPUT "${PEACH_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/peach_native.py"
            "${SM64DS_DECOMP_DIR}" "${PEACH_NATIVE}"
        DEPENDS tools/peach_native.py tools/actor_pmfs.py
            "${SM64DS_DECOMP_DIR}/src/__sinit_ov085_0212f3a0.c"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov085/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0085.bin")
    target_sources(smoke_player PRIVATE "${PEACH_NATIVE}")
endif()

if(NINTENDO_3DS)
    set(KANBAN_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/kanban_native.cpp")
    add_custom_command(OUTPUT "${KANBAN_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/kanban_native.py"
            "${SM64DS_DECOMP_DIR}" "${KANBAN_NATIVE}"
        DEPENDS tools/kanban_native.py "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0085.bin")
    target_sources(smoke_player PRIVATE "${KANBAN_NATIVE}")
endif()

if(NINTENDO_3DS)
    set(MIP_KEY_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/mip_key_native.cpp")
    add_custom_command(OUTPUT "${MIP_KEY_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mip_key_native.py"
            "${SM64DS_DECOMP_DIR}" "${MIP_KEY_NATIVE}"
        DEPENDS tools/mip_key_native.py tools/actor_pmfs.py
            "${SM64DS_DECOMP_DIR}/src/__sinit_ov085_0212f9bc.c"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov085/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0085.bin")
    target_sources(smoke_player PRIVATE "${MIP_KEY_NATIVE}")
endif()

if(NINTENDO_3DS)
    set(OVERLAY85_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/overlay85_native.cpp")
    add_custom_command(OUTPUT "${OVERLAY85_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay85_native.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY85_NATIVE}"
        DEPENDS tools/overlay85_native.py
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov085/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0085.bin")
    target_sources(smoke_player PRIVATE "${OVERLAY85_NATIVE}" unmatched/overlay85.cpp)
endif()

if(NINTENDO_3DS)
    set(WATERBOMB_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/waterbomb_native.cpp")
    add_custom_command(OUTPUT "${WATERBOMB_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/waterbomb_native.py"
            "${SM64DS_DECOMP_DIR}" "${WATERBOMB_NATIVE}"
        DEPENDS tools/waterbomb_native.py tools/actor_pmfs.py
            "${SM64DS_DECOMP_DIR}/src/__sinit_ov098_0213c2b4.c"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov098/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0098.bin")
    target_sources(smoke_player PRIVATE "${WATERBOMB_NATIVE}")
endif()

if(NINTENDO_3DS)
    set(ARROWS_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/arrows_native.cpp")
    add_custom_command(OUTPUT "${ARROWS_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/arrows_native.py"
            "${SM64DS_DECOMP_DIR}" "${ARROWS_NATIVE}"
        DEPENDS tools/arrows_native.py
            "${SM64DS_DECOMP_DIR}/src/actors/daObjYajirusi_c.cpp"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov098/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0098.bin")
    target_sources(smoke_player PRIVATE "${ARROWS_NATIVE}")
endif()

if(NINTENDO_3DS)
    set(SMALLBLOCK_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/smallblock_native.cpp")
    add_custom_command(OUTPUT "${SMALLBLOCK_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/smallblock_native.py"
            "${SM64DS_DECOMP_DIR}" "${SMALLBLOCK_NATIVE}"
        DEPENDS tools/smallblock_native.py tools/actor_pmfs.py
            "${SM64DS_DECOMP_DIR}/src/__sinit_ov098_0213c058.c"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov098/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0098.bin")
    target_sources(smoke_player PRIVATE "${SMALLBLOCK_NATIVE}")
endif()

if(NOT MSVC)
    # Original ABI deliberately accepts a null Player reference on this path.
    set_source_files_properties("${SM64DS_DECOMP_DIR}/src/_ZN8dActor_c15GivePlayerCoinsER6Playerhj.cpp"
        PROPERTIES COMPILE_OPTIONS "-fno-delete-null-pointer-checks")
endif()

if(NINTENDO_3DS)
    set(OVERLAY98_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/overlay98_native.cpp")
    add_custom_command(OUTPUT "${OVERLAY98_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay98_native.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY98_NATIVE}"
        DEPENDS tools/overlay98_native.py
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov098/symbols.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0098.bin")
    target_sources(smoke_player PRIVATE "${OVERLAY98_NATIVE}" unmatched/overlay98.cpp)
endif()

if(NINTENDO_3DS)
    target_sources(smoke_player PRIVATE platform/3ds/console_sleep.cpp)
endif()

if(NINTENDO_3DS)
    file(GLOB SSEQ_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sseqplayer/*.cpp")
    add_library(sm64ds_sseq STATIC ${SSEQ_CORE_SOURCES} audio/sseq_decoder.cpp audio/sound_commands.cpp audio/sound_output_3ds.cpp)
    target_compile_definitions(sm64ds_sseq PRIVATE Player=Sm64dsNitroPlayer Track=Sm64dsNitroTrack Channel=Sm64dsNitroChannel)
    target_sources(smoke_player PRIVATE audio/sound_native.cpp
        "${SM64DS_DECOMP_DIR}/src/func_0205b358.c"
        "${SM64DS_DECOMP_DIR}/src/func_0205b554.c"
        "${SM64DS_DECOMP_DIR}/src/func_0205b608.c")
    target_link_libraries(smoke_player PRIVATE sm64ds_sseq)
endif()

if(NINTENDO_3DS)
    target_sources(smoke_player PRIVATE "${SM64DS_DECOMP_DIR}/src/_ZN4Heap7DestroyEv.cpp")
endif()

# Native overlay62 actor code and checked resource lifecycle.
if(NINTENDO_3DS)
    sm64ds_hostgen_symbols(OVERLAY62_ACTORS daHolhei_c_classInit
        daNknk_c_classInit_NOKONOKO daRNk_c_classInit daRFlag_c_classInit daJango_c_classInit)
    set(OVERLAY62_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/overlay62_native.cpp")
    add_custom_command(OUTPUT "${OVERLAY62_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay62_native.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY62_NATIVE}"
        DEPENDS tools/overlay62_native.py ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov062/symbols.txt ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov062/relocs.txt
            ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0062.bin ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin)
    add_library(sm64ds_overlay62 OBJECT ${OVERLAY62_ACTORS} "${OVERLAY62_NATIVE}"
        ${SM64DS_DECOMP_DIR}/src/__sinit_ov062_0211cf30.c ${SM64DS_DECOMP_DIR}/src/__sinit_ov062_0211d2d8.c
        ${SM64DS_DECOMP_DIR}/src/__sinit_ov062_0211d4a0.c ${SM64DS_DECOMP_DIR}/src/__sinit_ov062_0211d690.c
        ${SM64DS_DECOMP_DIR}/src/__sinit_ov062_0211d6fc.c)
    target_sources(sm64ds_overlay62 PRIVATE
        ${SM64DS_DECOMP_DIR}/src/func_020165c4.c ${SM64DS_DECOMP_DIR}/src/func_0204531c.c
        ${SM64DS_DECOMP_DIR}/src/func_020462bc.c ${SM64DS_DECOMP_DIR}/src/func_020462b4.c ${SM64DS_DECOMP_DIR}/src/func_02047910.c
        ${SM64DS_DECOMP_DIR}/src/_ZN14BlendModelAnim6RenderEPK7Vector3.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN14BlendModelAnim9Virtual10ER9Matrix4x3.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN14BlendModelAnim9DoSetFileEPcii.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN14BlendModelAnim11UpdateVertsEv.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN14BlendModelAnim9Virtual18EjPK7Vector3.cpp
        ${SM64DS_DECOMP_DIR}/src/LoadBlueCoinModel.c
        ${SM64DS_DECOMP_DIR}/src/UnloadBlueCoinModel.c
        ${SM64DS_DECOMP_DIR}/src/_ZN14BlendModelAnim7AdvanceEv.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN14BlendModelAnim7SetAnimER8BCA_Fileii5Fix12IiEt.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN14BlendModelAnimC1Ev.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN14BlendModelAnimD1Ev.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN5Model12HideMaterialEii.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN5Model12ShowMaterialEii.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN5Sound22LoadAndSetMusic_Layer2Ej.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN5Sound22StopLoadedMusic_Layer2Ev.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN6Player6BounceE5Fix12IiE.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN8Particle19SetSelfDestructFlagEj.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN8Particle6System12FromUniqueIDEj.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN8SaveData13PlayerLoseCapEv.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN8dActor_c14GetSubtractionEss.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN8dActor_c18ClosestWithActorIDEj.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN8dActor_c19MakeVanishLuigiWorkER5dCc_c.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN8dActor_c22IsTooFarAwayFromPlayerE5Fix12IiE.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN8dActor_c24BumpedUnderneathByPlayerER6Player.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN8dActor_c8PoofDustEv.cpp
    )
    target_sources(smoke_player PRIVATE $<TARGET_OBJECTS:sm64ds_overlay62> unmatched/overlay62.cpp)
    target_link_libraries(sm64ds_overlay62 PRIVATE ntr)
    target_include_directories(sm64ds_overlay62 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
    target_compile_options(sm64ds_overlay62 PRIVATE -fno-strict-aliasing -Wno-narrowing $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
endif()

if(NINTENDO_3DS)
    set(OVERLAY89_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/overlay89_native.cpp")
    add_custom_command(OUTPUT "${OVERLAY89_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay89_native.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY89_NATIVE}"
        DEPENDS tools/overlay89_native.py ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov089/symbols.txt
            ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0089.bin ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin)
    target_sources(smoke_player PRIVATE "${OVERLAY89_NATIVE}" unmatched/overlay89.cpp ${SM64DS_DECOMP_DIR}/src/__sinit_ov089_021328d4.c
        ${SM64DS_DECOMP_DIR}/src/_ZN5Event8ClearBitEj.cpp ${SM64DS_DECOMP_DIR}/src/_ZN5Sound22LoadAndSetMusic_Layer3Ej.cpp
        ${SM64DS_DECOMP_DIR}/src/__sinit_ov002_02106e40.c)
endif()

if(NINTENDO_3DS)
    set(STAR_STATES "${CMAKE_CURRENT_BINARY_DIR}/star_states.cpp")
    add_custom_command(OUTPUT "${STAR_STATES}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/star_states.py"
            "${SM64DS_DECOMP_DIR}" "${STAR_STATES}"
        DEPENDS tools/star_states.py ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/symbols.txt
            ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin ${SM64DS_DECOMP_DIR}/config/arm9/relocs.txt
            ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin)
    target_sources(smoke_player PRIVATE "${STAR_STATES}"
        ${SM64DS_DECOMP_DIR}/src/DeathTable_ClearBit.c ${SM64DS_DECOMP_DIR}/src/_ZN10dBgCh_Actr22ClearJustHitGroundFlagEv.cpp
        ${SM64DS_DECOMP_DIR}/src/func_0203d024.c ${SM64DS_DECOMP_DIR}/src/_ZN8dActor_c14FarthestPlayerEv.cpp
        ${SM64DS_DECOMP_DIR}/src/NumCoins.c ${SM64DS_DECOMP_DIR}/src/NumVsStarsObtained.c)
endif()

if(NINTENDO_3DS)
    sm64ds_hostgen_symbols(OVERLAY94_ACTORS daOwl_c_classInit)
    set(OVERLAY94_NATIVE "${CMAKE_CURRENT_BINARY_DIR}/overlay94_native.cpp")
    add_custom_command(OUTPUT "${OVERLAY94_NATIVE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay94_native.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY94_NATIVE}"
        DEPENDS tools/overlay94_native.py ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov094/symbols.txt
            ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0094.bin ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin)
    target_sources(smoke_player PRIVATE ${OVERLAY94_ACTORS} "${OVERLAY94_NATIVE}" unmatched/overlay94.cpp
        ${SM64DS_DECOMP_DIR}/src/__sinit_ov094_021367e8.c
        ${SM64DS_DECOMP_DIR}/src/func_ov002_020df840.c ${SM64DS_DECOMP_DIR}/src/func_ov002_020df7ac.c ${SM64DS_DECOMP_DIR}/src/func_ov002_020df7f4.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN10dBgCh_Actr12Unk_0203589cEv.cpp)
endif()

# Native overlay100 actors and dependencies, linked into the production loader.
if(NINTENDO_3DS)
    sm64ds_hostgen_symbols(OVERLAY100_ACTORS daIbl_c_classInit daWanwan2_c_classInit _ZN11daWanwan2_cD1Ev
        _ZN16dPathLiftActor_c12BaseBehaviorEv
        daStarGate_c_classInit daObjPathLift_c_classInit)
    add_library(sm64ds_overlay100 OBJECT ${OVERLAY100_ACTORS}
        ${SM64DS_DECOMP_DIR}/src/Matrix4x3_FromRotationXYZExt.c
        ${SM64DS_DECOMP_DIR}/src/_ZN4dBgW16UpdatePosAndAngsERS_P8dActor_cR5dBgPiR7Vector3P10Vector3_16S8_.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN8dActor_c15HugeLandingDustEb.cpp
        ${SM64DS_DECOMP_DIR}/src/func_020393c4.c ${SM64DS_DECOMP_DIR}/src/func_020393d4.c ${SM64DS_DECOMP_DIR}/src/func_0203d73c.c ${SM64DS_DECOMP_DIR}/src/func_ov002_020ad660.cpp
        ${SM64DS_DECOMP_DIR}/src/__sinit_ov100_021474e8.c ${SM64DS_DECOMP_DIR}/src/__sinit_ov100_021475a4.c
        ${SM64DS_DECOMP_DIR}/src/__sinit_ov100_02147d7c.c
        ${SM64DS_DECOMP_DIR}/src/_ZN11CommonModelC1Ev.cpp ${SM64DS_DECOMP_DIR}/src/_ZN11CommonModelD1Ev.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN11CommonModel6RenderEPK7Vector3.cpp ${SM64DS_DECOMP_DIR}/src/_ZN11CommonModel9DoSetFileEPcii.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN11CommonModel12SetPolygonIDEj.cpp ${SM64DS_DECOMP_DIR}/src/_ZN11CommonModel13Func_020160ACEj.cpp)
    target_link_libraries(sm64ds_overlay100 PRIVATE ntr)
    target_include_directories(sm64ds_overlay100 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
    target_compile_options(sm64ds_overlay100 PRIVATE -fno-strict-aliasing -Wno-narrowing $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
endif()

if(NINTENDO_3DS)
    set(OVERLAY100_STORAGE "${CMAKE_CURRENT_BINARY_DIR}/overlay100_storage.cpp")
    add_custom_command(OUTPUT "${OVERLAY100_STORAGE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay100_storage.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY100_STORAGE}"
        DEPENDS tools/overlay100_storage.py ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov100/symbols.txt)
    target_sources(smoke_player PRIVATE "${OVERLAY100_STORAGE}")
endif()

if(NINTENDO_3DS)
    set(OVERLAY100_DEPENDENCIES "${CMAKE_CURRENT_BINARY_DIR}/overlay100_dependencies.cpp")
    add_custom_command(OUTPUT "${OVERLAY100_DEPENDENCIES}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay100_dependencies.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY100_DEPENDENCIES}"
        DEPENDS tools/overlay100_dependencies.py ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov100/symbols.txt
            ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/symbols.txt ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0100.bin
            ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin)
    target_sources(sm64ds_overlay100 PRIVATE "${OVERLAY100_DEPENDENCIES}"
        ${SM64DS_DECOMP_DIR}/src/__sinit_ov002_021071f4.c
        ${SM64DS_DECOMP_DIR}/src/_ZN4dBgW22UpdatePosWithTransformERS_P8dActor_cR5dBgPiR7Vector3P10Vector3_16S8_.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN4dBgW25UpdateAngsWithAngularVelYERS_P8dActor_cR5dBgPiR7Vector3P10Vector3_16S8_.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN8dActor_c13LandingDustAtER7Vector3b.cpp)
endif()

if(NINTENDO_3DS)
    set(OVERLAY100_PROFILES "${CMAKE_CURRENT_BINARY_DIR}/overlay100_profiles.cpp")
    add_custom_command(OUTPUT "${OVERLAY100_PROFILES}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay100_profiles.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY100_PROFILES}"
        DEPENDS tools/overlay100_profiles.py ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0100.bin ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin)
    target_sources(smoke_player PRIVATE $<TARGET_OBJECTS:sm64ds_overlay100> "${OVERLAY100_PROFILES}" unmatched/overlay100.cpp)
endif()

# Native overlay102 actor implementations and relocated resources.
if(NINTENDO_3DS)
    sm64ds_hostgen_symbols(OVERLAY102_ACTORS daObjSimpleBg_c_classInit_MC_HAZAD daShl_c_classInit)
    add_library(sm64ds_overlay102 OBJECT ${OVERLAY102_ACTORS}
        ${SM64DS_DECOMP_DIR}/src/__sinit_ov102_0214d714.c ${SM64DS_DECOMP_DIR}/src/__sinit_ov102_0214dfac.c)
    target_link_libraries(sm64ds_overlay102 PRIVATE ntr)
    target_include_directories(sm64ds_overlay102 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
    target_compile_options(sm64ds_overlay102 PRIVATE -fno-strict-aliasing -Wno-narrowing $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
endif()

if(NINTENDO_3DS)
    set(OVERLAY102_STORAGE "${CMAKE_CURRENT_BINARY_DIR}/overlay102_storage.cpp")
    add_custom_command(OUTPUT "${OVERLAY102_STORAGE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay102_storage.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY102_STORAGE}"
        DEPENDS tools/overlay102_storage.py ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov102/symbols.txt ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0102.bin)
    target_sources(smoke_player PRIVATE "${OVERLAY102_STORAGE}")
    set(SHELL_PMFS "${CMAKE_CURRENT_BINARY_DIR}/shell_pmfs.cpp")
    add_custom_command(OUTPUT "${SHELL_PMFS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/actor_pmfs.py"
            "${SM64DS_DECOMP_DIR}" 102 0x02148a80
            src/__sinit_ov102_0214dfac.c "${SHELL_PMFS}"
        DEPENDS tools/actor_pmfs.py ${SM64DS_DECOMP_DIR}/src/__sinit_ov102_0214dfac.c
            ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov102/symbols.txt ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0102.bin)
    target_sources(sm64ds_overlay102 PRIVATE "${SHELL_PMFS}")
endif()

if(NINTENDO_3DS)
    set(OVERLAY102_SIMPLE_BG "${CMAKE_CURRENT_BINARY_DIR}/overlay102_simple_bg.cpp")
    add_custom_command(OUTPUT "${OVERLAY102_SIMPLE_BG}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay102_simple_bg.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY102_SIMPLE_BG}"
        DEPENDS tools/overlay102_simple_bg.py ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0102.bin ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin)
    target_sources(sm64ds_overlay102 PRIVATE "${OVERLAY102_SIMPLE_BG}"
        ${SM64DS_DECOMP_DIR}/src/_ZN6Player16IsInsideOfCannonEv.cpp ${SM64DS_DECOMP_DIR}/src/func_02010304.cpp
        ${SM64DS_DECOMP_DIR}/src/_ZN8dActor_c7FindEggER5dCc_c.cpp)
endif()

if(NINTENDO_3DS)
    set(OVERLAY102_PROFILES "${CMAKE_CURRENT_BINARY_DIR}/overlay102_profiles.cpp")
    add_custom_command(OUTPUT "${OVERLAY102_PROFILES}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay102_profiles.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY102_PROFILES}"
        DEPENDS tools/overlay102_profiles.py ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0102.bin ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin)
    target_sources(smoke_player PRIVATE $<TARGET_OBJECTS:sm64ds_overlay102> "${OVERLAY102_PROFILES}" unmatched/overlay102.cpp)
endif()

if(NINTENDO_3DS)
    target_sources(smoke_player PRIVATE ${SM64DS_DECOMP_DIR}/src/engine/message/func_0201fe08.c ${SM64DS_DECOMP_DIR}/src/engine/message/func_0201cb2c.c)
    # Original sound boot retained as an explicit integration target. Its stream
    # worker and resource-cache hooks need native service adaptation before use.
    add_library(sm64ds_sound_boot OBJECT EXCLUDE_FROM_ALL ${SM64DS_DECOMP_DIR}/src/func_020133bc.c)
    target_link_libraries(sm64ds_sound_boot PRIVATE ntr)
    target_include_directories(sm64ds_sound_boot PRIVATE "${SM64DS_DECOMP_DIR}/include")
endif()

if(NINTENDO_3DS)
    set(CASTLE_PROFILES "${CMAKE_CURRENT_BINARY_DIR}/castle_profiles.cpp")
    add_custom_command(OUTPUT "${CASTLE_PROFILES}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/castle_profiles.py"
            "${SM64DS_DECOMP_DIR}" "${CASTLE_PROFILES}"
        DEPENDS tools/castle_profiles.py ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0009.bin ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin)
    target_sources(smoke_player PRIVATE "${CASTLE_PROFILES}")
endif()

if(NINTENDO_3DS)
    set(COMMON_ACTOR_STATES "${CMAKE_CURRENT_BINARY_DIR}/common_actor_states.cpp")
    add_custom_command(OUTPUT "${COMMON_ACTOR_STATES}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/common_actor_states.py"
            "${SM64DS_DECOMP_DIR}" "${COMMON_ACTOR_STATES}"
        DEPENDS tools/common_actor_states.py tools/actor_pmfs.py
            ${SM64DS_DECOMP_DIR}/src/__sinit_ov002_02100c50.c ${SM64DS_DECOMP_DIR}/src/__sinit_ov002_02107118.c
            ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/symbols.txt ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin
            ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov079/symbols.txt ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov079/relocs.txt
            ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0079.bin tools/native_overlay_data.py)
    sm64ds_hostgen_symbols(COMMON_ACTOR_STATE_DEPENDENCIES
        _ZN8dActor_c18HorzAngleToFPlayerEv _ZNK10dBgCh_Actr14GetResultFlag1Ev
        func_ov002_020d5f98 func_ov002_020d6048)
    target_sources(smoke_player PRIVATE "${COMMON_ACTOR_STATES}"
        ${COMMON_ACTOR_STATE_DEPENDENCIES}
        ${SM64DS_DECOMP_DIR}/src/__sinit_ov002_02100c50.c ${SM64DS_DECOMP_DIR}/src/__sinit_ov002_02107118.c)
    set(COMMON_SCENERY_PROFILES "${CMAKE_CURRENT_BINARY_DIR}/common_scenery_profiles.cpp")
    add_custom_command(OUTPUT "${COMMON_SCENERY_PROFILES}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/common_scenery_profiles.py"
            "${SM64DS_DECOMP_DIR}" "${COMMON_SCENERY_PROFILES}"
        DEPENDS tools/common_scenery_profiles.py ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin)
    target_sources(smoke_player PRIVATE "${COMMON_SCENERY_PROFILES}")
endif()

if(NINTENDO_3DS)
    sm64ds_hostgen_symbols(MAP_NATIVE dMap_c_classInit dMeter_c_classInit func_0203da4c)
    add_library(sm64ds_map OBJECT EXCLUDE_FROM_ALL ${MAP_NATIVE})
    target_link_libraries(sm64ds_map PRIVATE ntr)
    target_include_directories(sm64ds_map PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
    target_compile_options(sm64ds_map PRIVATE -fno-strict-aliasing -Wno-narrowing $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
endif()

if(NINTENDO_3DS)
    target_sources(sm64ds_map PRIVATE
        ${SM64DS_DECOMP_DIR}/src/GetMinimapScale.c ${SM64DS_DECOMP_DIR}/src/UpdateMinimap.c ${SM64DS_DECOMP_DIR}/src/Vec3_DivScalarInPlace.c
        ${SM64DS_DECOMP_DIR}/src/func_0202a958.c ${SM64DS_DECOMP_DIR}/src/func_0202a96c.c ${SM64DS_DECOMP_DIR}/src/func_0202a980.c
        ${SM64DS_DECOMP_DIR}/src/_ZN3OAM6RenderEbP7OamAttriiii5Fix12IiEi.c
        ${SM64DS_DECOMP_DIR}/src/_ZN3OAM9RenderSubEP7OamAttriiii.cpp)
    target_sources(smoke_player PRIVATE $<TARGET_OBJECTS:sm64ds_map>)
endif()

if(NINTENDO_3DS)
    sm64ds_hostgen_symbols(DEMO_NATIVE daDemo_c_classInit
        _ZN14CutsceneObjectD1Ev _ZN14CutsceneObject13InitResourcesEv
        _ZN14CutsceneObject16CleanupResourcesEv _ZN14CutsceneObject8BehaviorEv
        _ZN14CutsceneObject6RenderEv _ZN14CutsceneObject16OnPendingDestroyEv
        func_ov002_020f23d0 func_ov002_020f23f0 func_ov002_020f63a0
        func_ov002_020f63d4 func_ov002_020f64ac func_ov002_020f65b8
        func_ov002_020f65ec func_ov002_020f6618 func_ov002_020f6960
        func_ov002_020f6a50
        func_ov002_020f1fcc func_ov002_020f20f4 func_ov002_020f2210
        func_ov002_020f2340 func_ov002_020f237c func_ov002_020f5f60
        func_ov002_020f5f8c func_ov002_020f5fb8
        func_ov002_020f26e0 func_ov002_020f3978 func_ov002_020f2eac
        func_ov002_020f2990 func_ov002_020f5990 func_ov002_020f37a0
        func_ov002_020f2dd4 func_ov002_020f27e8
        func_ov002_020f2790 func_ov002_020f39ec __cxa_vec_ctor
        __sinit_ov002_02107370
        func_ov002_020f2958
        func_ov002_020f2a48
        func_ov002_020f2f18
        func_ov002_020f2f64
        func_ov002_020f2f9c
        func_ov002_020f37fc
        func_ov002_020f3828
        func_ov002_020f3954
        func_ov002_020f5a94
        func_ov002_020f2a78
        func_ov002_020f2a88
        func_ov002_020f2aec
        func_ov002_020f2bf4
        func_ov002_020f2d0c
        func_ov002_020f2d70
        func_ov002_020f30f8
        func_ov002_020f32e4
        func_ov002_020f3310
        func_ov002_020f335c
        func_ov002_020f340c
        func_ov002_020f34bc
        func_ov002_020f35e4
        func_ov002_020f3740
        func_ov002_020f378c
        func_ov002_020f3ae8
        func_ov002_020f3ba0
        func_ov002_020f3d38
        func_ov002_020f3d98
        func_ov002_020f3de4
        func_ov002_020f40fc
        func_ov002_020f43cc
        func_ov002_020f4710
        func_ov002_020f4a2c
        func_ov002_020f4d70
        func_ov002_020f5010
        func_ov002_020f5328
        func_ov002_020f55b4
        func_ov002_020f562c
        func_ov002_020f5678
        func_ov002_020f569c
        func_ov002_020f57c0
        func_ov002_020f5848
        func_ov002_020f5894
        func_ov002_020f5b24
        func_ov002_020f5b7c
        func_ov002_020f5b80
        func_ov002_020f5b84
        func_ov002_020f5b88
        func_ov002_020f5b8c
        func_ov002_020f5b98
        func_ov002_020f5bcc
        func_ov002_020f5bf4
        func_ov002_020f5c40
        func_ov002_020f5c88
        func_ov002_020f5cd0
        func_ov002_020f5d34
        func_ov002_020f5e18
        func_ov002_020f5e38
        func_ov002_020f5e58
        func_ov002_020f5e78
        func_ov002_020f5e98
        func_ov002_020f5eb8
        func_ov002_020f5ed8
        func_ov002_020f5ee4
        func_ov002_020f5ef0
        func_ov002_020f5efc)
    add_library(sm64ds_demo OBJECT EXCLUDE_FROM_ALL ${DEMO_NATIVE})
    target_link_libraries(sm64ds_demo PRIVATE ntr)
    target_include_directories(sm64ds_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
    target_compile_options(sm64ds_demo PRIVATE -fno-strict-aliasing -Wno-narrowing $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
endif()

if(NINTENDO_3DS)
    set(DEMO_TABLES "${CMAKE_CURRENT_BINARY_DIR}/demo_tables.cpp")
    add_custom_command(OUTPUT "${DEMO_TABLES}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/demo_tables.py"
            "${SM64DS_DECOMP_DIR}" "${DEMO_TABLES}"
        DEPENDS tools/demo_tables.py ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin
            ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/symbols.txt ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/relocs.txt)
    target_sources(sm64ds_demo PRIVATE "${DEMO_TABLES}" unmatched/demo_models.cpp)
endif()

if(NINTENDO_3DS)
    target_sources(smoke_player PRIVATE $<TARGET_OBJECTS:sm64ds_demo>)
endif()

if(NINTENDO_3DS)
    set(DEMO_PMFS "${CMAKE_CURRENT_BINARY_DIR}/demo_pmfs.cpp")
    add_custom_command(OUTPUT "${DEMO_PMFS}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/actor_pmfs.py"
            "${SM64DS_DECOMP_DIR}" 2 0x020ad660 src/__sinit_ov002_02107370.c "${DEMO_PMFS}"
        DEPENDS tools/actor_pmfs.py ${SM64DS_DECOMP_DIR}/src/__sinit_ov002_02107370.c
            ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov002/symbols.txt ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0002.bin)
    target_sources(sm64ds_demo PRIVATE "${DEMO_PMFS}")
endif()

if(NINTENDO_3DS)
    sm64ds_hostgen_symbols(SOUND_BOOT_NATIVE
        func_0205130c func_02051024 func_02050f34 func_02050d54
        func_0204f070 func_0204f94c func_02052008 func_02011a28 func_02048f34
        func_02048fd4 func_0204f1e8 func_0204fa3c func_0204fc40
        func_02050950 func_02050970 func_0205097c func_02050988
        func_02050ae8 func_0205a82c func_020512f0 func_0204f278 func_0205b524
        func_02049cd8 func_02049ba8 func_020499bc func_020497a0
        func_020507e0 func_02050798 func_020503a4
        func_0204f0d8 func_0204f138 func_0204f194
        func_0205aa10 func_0205ab28 func_0205ab6c func_0205aac8 func_0205b4d0)
    target_sources(sm64ds_sound_boot PRIVATE ${SOUND_BOOT_NATIVE})
    target_include_directories(sm64ds_sound_boot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(sm64ds_sound_boot PRIVATE -fno-strict-aliasing -Wno-narrowing $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
endif()

if(NINTENDO_3DS)
    target_sources(sm64ds_sound_boot PRIVATE unmatched/sound_boot_storage.cpp audio/sound_worker.cpp
        ${SM64DS_DECOMP_DIR}/src/func_02058940.c ${SM64DS_DECOMP_DIR}/src/func_02058894.c ${SM64DS_DECOMP_DIR}/src/func_020500a4.c ${SM64DS_DECOMP_DIR}/src/func_0205b470.c)
    target_sources(smoke_player PRIVATE $<TARGET_OBJECTS:sm64ds_sound_boot>)
    target_sources(smoke_player PRIVATE unmatched/model_vram_trace.cpp)
    target_sources(smoke_player PRIVATE unmatched/sound_archive_trace.cpp)
    target_link_options(smoke_player PRIVATE "-Wl,--wrap=func_02050a84"
        "-Wl,--wrap=_ZN5Sound17InfoSequenceEntry9GetWithIDEj")
    target_sources(smoke_player PRIVATE ${SM64DS_DECOMP_DIR}/src/SetNumPlayers.c ${SM64DS_DECOMP_DIR}/src/func_020308d0.c)
    target_sources(smoke_player PRIVATE ${SM64DS_DECOMP_DIR}/src/func_02019404.c ${SM64DS_DECOMP_DIR}/src/_ZN3OAM5ResetEv.cpp)
    sm64ds_hostgen_symbols(STAGE_FRAME_PREPARE func_02019390 func_02018ec0
        func_02018efc func_0202345c func_0200f4b4 func_0200f468 func_020557b4 func_02055624)
    target_sources(smoke_player PRIVATE ${STAGE_FRAME_PREPARE})
    # PerspectiveW reuses the divider mode programmed by synchronous fdiv.
    # The CPU-only HAL quotient loses this observable hardware state.
    sm64ds_hostgen_symbols(STAGE_PROJECTION_DIVIDE _ZN4cstd4fdivEii
        _ZN4cstd10fdiv_asyncE5Fix12IiE5Fix12IiE)
    target_sources(smoke_player PRIVATE ${STAGE_PROJECTION_DIVIDE})
    target_link_options(smoke_player PRIVATE "-Wl,--wrap=_ZN5Model13GetVramOffsetEj")
    target_link_options(smoke_player PRIVATE "-Wl,--wrap=_ZN5Model13LoadTexAndPalER8BMD_File")
endif()

# Retail main menu: original overlay7 plus dedicated native ABI/data adapters.
if(NINTENDO_3DS)
    include("${CMAKE_CURRENT_SOURCE_DIR}/menu/menu.cmake")
    target_sources(smoke_player PRIVATE $<TARGET_OBJECTS:sm64ds_menu>)
endif()

include(menu/minigame_menu.cmake)

include(menu/wanted.cmake)

# Castle interior overlay10: three original actor translation units and sinit.
if(NINTENDO_3DS)
    sm64ds_hostgen_symbols(OVERLAY10_ACTORS daObjC1_Trap_c_classInit daObjC1Hikari_c_classInit daObjC1Peach_c_classInit
        _ZN5Sound20PlaySmallSecretSoundEP8dActor_cPt)
    set(OVERLAY10_STORAGE "${CMAKE_CURRENT_BINARY_DIR}/overlay10_native.cpp")
    add_custom_command(OUTPUT "${OVERLAY10_STORAGE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay10_native.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY10_STORAGE}"
        DEPENDS tools/overlay10_native.py tools/native_overlay_data.py ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov010/symbols.txt
            ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0010.bin ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin)
    add_library(sm64ds_overlay10 OBJECT ${OVERLAY10_ACTORS} "${OVERLAY10_STORAGE}"
        ${SM64DS_DECOMP_DIR}/src/__sinit_ov010_0211203c.c ${SM64DS_DECOMP_DIR}/src/__sinit_ov010_0211211c.c ${SM64DS_DECOMP_DIR}/src/__sinit_ov010_0211215c.c
        unmatched/overlay10.cpp)
    target_link_libraries(sm64ds_overlay10 PRIVATE ntr)
    target_include_directories(sm64ds_overlay10 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
    target_compile_options(sm64ds_overlay10 PRIVATE -fno-strict-aliasing -Wno-narrowing $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
    target_sources(smoke_player PRIVATE $<TARGET_OBJECTS:sm64ds_overlay10>)
endif()

# Boo and haunted actor group, required by the normal castle interior load.
if(NINTENDO_3DS)
    sm64ds_hostgen_symbols(OVERLAY63_ACTORS daTrs_c_classInit_TERESA
        _ZN11daTrsTrap_c13InitResourcesEv daTrsTrap_c_classInit_KAIDAN
        daObjTh_Fall_Block_c_classInit daPiano_c_classInit daObjFallBlock_c_InitResources
        _ZN11dCapEnemy_cC2Ev _ZN11dCapEnemy_cD1Ev
        _ZN11dCapEnemy_c6AddCapEj _ZN11dCapEnemy_c21DestroyIfCapNotNeededEv
        _ZN11dCapEnemy_c16GetCapEatenOffItERK7Vector3 _ZN11dCapEnemy_c10ReleaseCapERK7Vector3
        _ZN11dCapEnemy_c15RespawnIfHasCapEv _ZN11dCapEnemy_c14RenderCapModelEPK7Vector3
        _ZN11dCapEnemy_c14UnloadCapModelEv _ZN11dCapEnemy_c12Unk_02005d94Ev
        _ZN11dCapEnemy_c11GetCapStateEv _ZN11dCapEnemy_c12UpdateCapPosERK7Vector3RK10Vector3_16
        _ZN5Sound15PlaySecretSoundEP8dActor_cPt _ZN5Sound22StopLoadedMusic_Layer3Ev
        _ZN8dActor_c13SpawnFireballERK7Vector3PK10Vector3_165Fix12IiES7_j
        func_0200f760 func_02011cfc func_02011d14 func_02005e28 func_02005ed8
        _ZN5Sound13Func_02048eb4Ev func_02048ed4)
    set(OVERLAY63_STORAGE "${CMAKE_CURRENT_BINARY_DIR}/overlay63_native.cpp")
    add_custom_command(OUTPUT "${OVERLAY63_STORAGE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay63_native.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY63_STORAGE}"
        DEPENDS tools/overlay63_native.py tools/native_overlay_data.py ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov063/symbols.txt
            ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov063/relocs.txt ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0063.bin ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin)
    add_library(sm64ds_overlay63 OBJECT ${OVERLAY63_ACTORS} "${OVERLAY63_STORAGE}"
        ${SM64DS_DECOMP_DIR}/src/unnamed/ov063/__sinit_ov063_0211e29c.c
        ${SM64DS_DECOMP_DIR}/src/unnamed/ov063/__sinit_ov063_0211e3cc.c
        ${SM64DS_DECOMP_DIR}/src/unnamed/ov063/__sinit_ov063_0211e590.c
        ${SM64DS_DECOMP_DIR}/src/game/actors/daPiano_c/__sinit_ov063_0211e5fc.c
        unmatched/overlay63.cpp)
    target_link_libraries(sm64ds_overlay63 PRIVATE ntr)
    target_include_directories(sm64ds_overlay63 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
    target_compile_options(sm64ds_overlay63 PRIVATE -fno-strict-aliasing -Wno-narrowing $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
    target_sources(smoke_player PRIVATE $<TARGET_OBJECTS:sm64ds_overlay63>)
endif()

# Original painting gate, mole and bouncing block group required by the castle.
if(NINTENDO_3DS)
    sm64ds_hostgen_symbols(OVERLAY80_ACTORS
        func_0201787c func_020178cc func_02017ae4
        func_02045a50 func_02045ad8
        _ZN11daChoropu_cD1Ev
        _ZN14daChoro_Rock_cD1Ev
        _ZN11daChoropu_c16OnAimedAtWithEggEv
        _ZN11daChoropu_c19func_ov080_02123860Ev
        _ZN11daChoropu_c19func_ov080_02123924Ev
        _ZN11daChoropu_c19func_ov080_02123a34Ev
        _ZN11daChoropu_c19func_ov080_02123c24Ev
        _ZN11daChoropu_c19func_ov080_02123eccEv
        _ZN11daChoropu_c19func_ov080_02123fccEv
        _ZN11daChoropu_c19func_ov080_02124088Ev
        _ZN11daChoropu_c19func_ov080_02124208Ev
        _ZN11daChoropu_c19func_ov080_02124360Ev
        _ZN11daChoropu_c19func_ov080_021243d8Ev
        func_ov080_02124418
        _ZN11daChoropu_c16CleanupResourcesEv
        _ZN14daChoro_Rock_c16CleanupResourcesEv
        _ZN11daChoropu_c6RenderEv
        _ZN14daChoro_Rock_c6RenderEv
        _ZN11daChoropu_c8BehaviorEv
        _ZN14daChoro_Rock_c8BehaviorEv
        _ZN11daChoropu_c13InitResourcesEv
        _ZN14daChoro_Rock_c13InitResourcesEv
        daChoro_Rock_c_classInit
        daChoropu_c_classInit
        _ZN9daBttBk_cD1Ev
        _ZN9daBttBk_c13OnYoshiTryEatEv
        _ZN9daBttBk_c19func_ov080_02124accEv
        _ZN9daBttBk_c19func_ov080_02124c3cEv
        _ZN9daBttBk_c19func_ov080_02124e60Ev
        _ZN9daBttBk_c19func_ov080_02124eb0Ev
        _ZN9daBttBk_c19func_ov080_02124edcEv
        _ZN9daBttBk_c19func_ov080_02124fecEv
        _ZN9daBttBk_c19func_ov080_0212500cEv
        _ZN9daBttBk_c19func_ov080_0212509cEv
        _ZN9daBttBk_c19func_ov080_021250c8Ev
        _ZN9daBttBk_c19func_ov080_02125104Ev
        _ZN9daBttBk_c19func_ov080_0212513cEi
        _ZN9daBttBk_c16CleanupResourcesEv
        _ZN9daBttBk_c16OnPendingDestroyEv
        _ZN9daBttBk_c6RenderEv
        _ZN9daBttBk_c8BehaviorEv
        _ZN9daBttBk_c13InitResourcesEv
        _ZN9daBttBk_c13OnTurnIntoEggER6Player
        daBttBk_c_classInit
        _ZN11daPicGate_cD1Ev
        func_ov080_02125460
        func_ov080_0212555c
        func_ov080_02125630
        func_ov080_021256f8
        func_ov080_02125940
        func_ov080_02125af0
        func_ov080_02125bb0
        func_ov080_02125cd4
        func_ov080_02125d08
        func_ov080_02125d64
        func_ov080_02125de0
        func_ov080_02125f00
        func_ov080_02125fd0
        func_ov080_02126120
        func_ov080_02126124
        func_ov080_021264ec
        func_ov080_021265ec
        func_ov080_0212677c
        func_ov080_021269b8
        func_ov080_02126a54
        _ZN11daPicGate_c16CleanupResourcesEv
        _ZN11daPicGate_c16OnPendingDestroyEv
        _ZN11daPicGate_c6RenderEv
        _ZN11daPicGate_c8BehaviorEv
        _ZN11daPicGate_c13InitResourcesEv
        daPicGate_c_classInit
        _ZN13daObjMaruta_cD1Ev
        _ZN13daObjMaruta_c15OnHitByMegaCharER6Player
        _ZN13daObjMaruta_c19func_ov080_02127094Ev
        func_ov080_021270dc
        _ZN13daObjMaruta_c6RenderEv
        func_ov080_0212714c
        func_ov080_021274ac
        func_ov080_0212758c
        func_ov080_02127658)
    set(OVERLAY80_STORAGE "${CMAKE_CURRENT_BINARY_DIR}/overlay80_native.cpp")
    add_custom_command(OUTPUT "${OVERLAY80_STORAGE}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay80_native.py"
            "${SM64DS_DECOMP_DIR}" "${OVERLAY80_STORAGE}"
        DEPENDS tools/overlay80_native.py tools/native_overlay_data.py ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov080/symbols.txt
            ${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov080/relocs.txt ${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_0080.bin ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin)
    add_library(sm64ds_overlay80 OBJECT ${OVERLAY80_ACTORS} "${OVERLAY80_STORAGE}"
        ${SM64DS_DECOMP_DIR}/src/__sinit_ov080_021278c0.c ${SM64DS_DECOMP_DIR}/src/__sinit_ov080_02127a60.c ${SM64DS_DECOMP_DIR}/src/__sinit_ov080_02127b2c.c
        ${SM64DS_DECOMP_DIR}/src/func_020178b4.c ${SM64DS_DECOMP_DIR}/src/func_02017a9c.c unmatched/overlay80.cpp unmatched/painting_021261f4.cpp)
    target_link_libraries(sm64ds_overlay80 PRIVATE ntr)
    target_include_directories(sm64ds_overlay80 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${SM64DS_DECOMP_DIR}/include")
    target_compile_options(sm64ds_overlay80 PRIVATE -fno-strict-aliasing -Wno-narrowing $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
    target_sources(smoke_player PRIVATE $<TARGET_OBJECTS:sm64ds_overlay80>)
endif()

# Audit the final native object graph, including sources appended by later
# integration batches. A newly imported original cannot bypass MMIO hostgen
# merely because it was absent from a hand-maintained symbol list.
if(NINTENDO_3DS)
    get_target_property(direct_memory_sources smoke_player SOURCES)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/direct_memory_sources.py")
    foreach(candidate IN LISTS direct_memory_sources)
        if(candidate MATCHES "^\\$<")
            continue()
        endif()
        get_filename_component(candidate_absolute "${candidate}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(candidate_absolute MATCHES "/src/.*\\.(c|cpp)$" AND EXISTS "${candidate_absolute}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${candidate_absolute}")
        endif()
    endforeach()
    string(REPLACE ";" "\n" direct_memory_source_lines "${direct_memory_sources}")
    set(direct_memory_input "${CMAKE_BINARY_DIR}/direct-memory-input.txt")
    file(WRITE "${direct_memory_input}" "${direct_memory_source_lines}\n")
    execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/direct_memory_sources.py"
        "${PORT_REPO_ROOT_ABS}" "${direct_memory_input}"
        RESULT_VARIABLE direct_memory_result OUTPUT_VARIABLE direct_memory_json
        ERROR_VARIABLE direct_memory_error)
    if(NOT direct_memory_result EQUAL 0)
        message(FATAL_ERROR "native memory audit failed: ${direct_memory_error}")
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/direct-memory-selected.json" "${direct_memory_json}")
    string(JSON direct_memory_count LENGTH "${direct_memory_json}")
    if(direct_memory_count GREATER 0)
        math(EXPR direct_memory_last "${direct_memory_count}-1")
        foreach(index RANGE 0 ${direct_memory_last})
            string(JSON original GET "${direct_memory_json}" ${index})
            file(RELATIVE_PATH relative "${PORT_REPO_ROOT_ABS}" "${original}")
            string(REGEX REPLACE "\\.(c|cpp)$" ".cpp" generated_relative "${relative}")
            set(generated "${CMAKE_BINARY_DIR}/host-src/${generated_relative}")
            add_custom_command(OUTPUT "${generated}"
                COMMAND "${PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/hostgen.py" --decomp "${SM64DS_DECOMP_DIR}"
                    --out "${CMAKE_BINARY_DIR}/host-src" --gcc-local-externs --native-primary-vptrs --source "${original}"
                DEPENDS "${original}" tools/hostgen.py tools/native_vtable_stores.py
                    tools/native_vtable_layouts.json ${SM64DS_DECOMP_DIR}/include/nitro/hw/registers.h)
            # Preserve include/ABI options attached to the original by earlier
            # integration work, even though the generated TU is now C++.
            get_source_file_property(original_options "${original}" COMPILE_OPTIONS)
            if(original_options)
                set_source_files_properties("${generated}" PROPERTIES COMPILE_OPTIONS "${original_options}")
            endif()
            set(retained_sources "")
            foreach(candidate IN LISTS direct_memory_sources)
                if(candidate MATCHES "^\\$<")
                    list(APPEND retained_sources "${candidate}")
                    continue()
                endif()
                get_filename_component(candidate_absolute "${candidate}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
                if(NOT candidate_absolute STREQUAL original)
                    list(APPEND retained_sources "${candidate}")
                endif()
            endforeach()
            set(direct_memory_sources "${retained_sources}")
            list(APPEND direct_memory_sources "${generated}")
        endforeach()
        list(REMOVE_DUPLICATES direct_memory_sources)
        set_property(TARGET smoke_player PROPERTY SOURCES "${direct_memory_sources}")
    endif()
    message(STATUS "Native direct-memory audit: ${direct_memory_count} original sources translated")
endif()

include(native_level_actors.cmake)

# Apply proved primary-vptr adaptations to remaining direct originals after
# all source imports/memory routing. Preserve source language and target options;
# turning these factories into C++ changes old unspecified argument prototypes.
if(NINTENDO_3DS)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        tools/native_vtable_sources.py tools/native_vtable_stores.py tools/native_vtable_layouts.json)
    get_property(native_abi_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(native_abi_outputs "")
    foreach(native_abi_target IN LISTS native_abi_targets)
        get_target_property(native_abi_sources ${native_abi_target} SOURCES)
        if(NOT native_abi_sources)
            continue()
        endif()
        string(REPLACE ";" "\n" native_abi_lines "${native_abi_sources}")
        set(native_abi_input "${CMAKE_BINARY_DIR}/native-vptr-${native_abi_target}-input.txt")
        file(WRITE "${native_abi_input}" "${native_abi_lines}\n")
        execute_process(COMMAND "${PYTHON}" tools/native_vtable_sources.py
            --root "${PORT_REPO_ROOT_ABS}" --source-list "${native_abi_input}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE native_abi_result OUTPUT_VARIABLE native_abi_json ERROR_VARIABLE native_abi_error)
        if(NOT native_abi_result EQUAL 0)
            message(FATAL_ERROR "native primary-vptr audit failed: ${native_abi_error}")
        endif()
        file(WRITE "${CMAKE_BINARY_DIR}/native-vptr-${native_abi_target}-selected.json" "${native_abi_json}")
        string(JSON native_abi_count LENGTH "${native_abi_json}")
        if(native_abi_count EQUAL 0)
            continue()
        endif()
        math(EXPR native_abi_last "${native_abi_count}-1")
        foreach(index RANGE 0 ${native_abi_last})
            string(JSON original GET "${native_abi_json}" ${index})
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
            file(RELATIVE_PATH relative "${PORT_REPO_ROOT_ABS}" "${original}")
            set(generated "${CMAKE_BINARY_DIR}/native-abi/${relative}")
            if(NOT generated IN_LIST native_abi_outputs)
                add_custom_command(OUTPUT "${generated}"
                    COMMAND "${PYTHON}" tools/native_vtable_sources.py --root "${PORT_REPO_ROOT_ABS}"
                        --source "${original}" --output "${generated}"
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    DEPENDS "${original}" tools/native_vtable_sources.py tools/native_vtable_stores.py
                        tools/native_vtable_layouts.json)
                list(APPEND native_abi_outputs "${generated}")
            endif()
            foreach(source_property COMPILE_OPTIONS COMPILE_DEFINITIONS INCLUDE_DIRECTORIES COMPILE_FLAGS LANGUAGE)
                get_source_file_property(source_value "${original}" ${source_property})
                if(source_value)
                    set_source_files_properties("${generated}" PROPERTIES ${source_property} "${source_value}")
                endif()
            endforeach()
            set(native_abi_retained "")
            foreach(candidate IN LISTS native_abi_sources)
                get_filename_component(candidate_absolute "${candidate}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
                if(NOT candidate_absolute STREQUAL original)
                    list(APPEND native_abi_retained "${candidate}")
                endif()
            endforeach()
            list(APPEND native_abi_retained "${generated}")
            set(native_abi_sources "${native_abi_retained}")
        endforeach()
        set_property(TARGET ${native_abi_target} PROPERTY SOURCES "${native_abi_sources}")
        message(STATUS "Native primary-vptr audit ${native_abi_target}: ${native_abi_count} originals adapted")
    endforeach()
endif()
