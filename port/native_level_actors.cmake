# Reviewed original actors/dependencies for native overlay modules. Keep this
# before the final ABI audit. Generated C stays C; generated C++
# uses the ordinary smoke_player compile flags and common GCC address points.
if(NINTENDO_3DS)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        tools/native_level_actor_sources.py tools/native_level_actor_sources.json
        tools/level_actor_cpp_adapters.py tools/native_actor_overlay_manifest.json
        tools/native_actor_lifecycle.py)
    execute_process(COMMAND "${PYTHON}" tools/native_level_actor_sources.py
        "${PORT_REPO_ROOT_ABS}" --plan WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE actor_plan_result OUTPUT_VARIABLE actor_plan ERROR_VARIABLE actor_plan_error)
    if(NOT actor_plan_result EQUAL 0)
        message(FATAL_ERROR "native actor source plan failed: ${actor_plan_error}")
    endif()
    string(JSON actor_source_count LENGTH "${actor_plan}")
    math(EXPR actor_source_last "${actor_source_count}-1")
    foreach(index RANGE 0 ${actor_source_last})
        string(JSON relative GET "${actor_plan}" ${index} source)
        string(JSON generated_relative GET "${actor_plan}" ${index} generated)
        set(generated "${CMAKE_BINARY_DIR}/native-actors/${generated_relative}")
        add_custom_command(OUTPUT "${generated}"
            COMMAND "${PYTHON}" tools/native_level_actor_sources.py "${PORT_REPO_ROOT_ABS}"
                --source "${relative}" --output "${generated}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            DEPENDS "${PORT_REPO_ROOT_ABS}/${relative}" tools/native_level_actor_sources.py
                tools/native_level_actor_sources.json tools/level_actor_cpp_adapters.py tools/hostgen.py
                tools/native_vtable_stores.py tools/native_vtable_layouts.json
                ${SM64DS_DECOMP_DIR}/include/nitro/hw/registers.h)
        target_sources(smoke_player PRIVATE "${generated}")
    endforeach()
    execute_process(COMMAND "${PYTHON}" tools/native_actor_lifecycle.py
        "${PORT_REPO_ROOT_ABS}" --plan WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE lifecycle_plan_result OUTPUT_VARIABLE lifecycle_plan ERROR_VARIABLE lifecycle_plan_error)
    if(NOT lifecycle_plan_result EQUAL 0)
        message(FATAL_ERROR "native actor lifecycle plan failed: ${lifecycle_plan_error}")
    endif()
    string(JSON import_count LENGTH "${lifecycle_plan}" imports)
    math(EXPR import_last "${import_count}-1")
    set(actor_lifecycle_inputs)
    foreach(index RANGE 0 ${import_last})
        string(JSON overlay GET "${lifecycle_plan}" imports ${index})
        set(generated "${CMAKE_BINARY_DIR}/native_actor_overlay${overlay}.cpp")
        set(overlay_config "00${overlay}")
        string(LENGTH "${overlay_config}" overlay_width)
        math(EXPR overlay_offset "${overlay_width}-3")
        string(SUBSTRING "${overlay_config}" ${overlay_offset} 3 overlay_config)
        set(overlay_file "0${overlay_config}")
        add_custom_command(OUTPUT "${generated}"
            COMMAND "${PYTHON}" tools/native_actor_overlay_imports.py
                "${PORT_REPO_ROOT_ABS}" ${overlay} "${generated}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            DEPENDS tools/native_actor_overlay_imports.py tools/native_actor_overlay_manifest.json
                tools/native_overlay_data.py ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin
                "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov${overlay_config}/symbols.txt"
                "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov${overlay_config}/relocs.txt"
                "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_${overlay_file}.bin")
        target_sources(smoke_player PRIVATE "${generated}")
        list(APPEND actor_lifecycle_inputs
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov${overlay_config}/symbols.txt"
            "${SM64DS_DECOMP_DIR}/config/arm9/overlays/ov${overlay_config}/delinks.txt"
            "${SM64DS_DECOMP_DIR}/extracted/overlays/overlay_${overlay_file}.bin")
    endforeach()
    set(actor_lifecycle "${CMAKE_BINARY_DIR}/native_actor_lifecycles.cpp")
    add_custom_command(OUTPUT "${actor_lifecycle}"
        COMMAND "${PYTHON}" tools/native_actor_lifecycle.py "${PORT_REPO_ROOT_ABS}"
            --output "${actor_lifecycle}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS tools/native_actor_lifecycle.py tools/native_actor_overlay_manifest.json
            ${SM64DS_DECOMP_DIR}/extracted/arm9_dec.bin ${actor_lifecycle_inputs})
    target_sources(smoke_player PRIVATE "${actor_lifecycle}" unmatched/native_actor_lifecycle.cpp
        unmatched/overlay3.cpp unmatched/level_actor_layouts.cpp
        unmatched/resident-relocation-prototype.cpp unmatched/resident_groups.cpp
        unmatched/resident_bindings.cpp
        unmatched/levels_sinit_ov044.c
        unmatched/levels_inherited_methods.cpp)
endif()
