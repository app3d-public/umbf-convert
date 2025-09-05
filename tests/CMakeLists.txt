set(UMBFTOOL_INPUT "data")
set(UMBFTOOL_INPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/data/input")
set(UMBFTOOL_OUTPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/data/output")

file(MAKE_DIRECTORY ${UMBFTOOL_OUTPUT_BUILD})

set(JSON_FILES
    texture
    target_texture
    material_color
    material_embedded
    material_targeted
    target_material
    scene_meshonly
    scene_embedded
    scene_targeted
    target_scene
    library_embedded
    library_targeted
    target_library
)

foreach(FILENAME ${JSON_FILES})
    configure_file(${UMBFTOOL_INPUT}/${FILENAME}.json.in
        ${UMBFTOOL_INPUT_BUILD}/${FILENAME}.json
        @ONLY)

    add_test(NAME umbf-convert_${FILENAME}
        COMMAND $<TARGET_FILE:umbf-convert>
        convert
        -i ${UMBFTOOL_INPUT_BUILD}/${FILENAME}.json
        -o ${UMBFTOOL_OUTPUT_BUILD}/${FILENAME}.umbf
        --format=json
    )
    set_tests_properties(umbf-convert_${FILENAME} PROPERTIES LABELS "umbftool")

    if(ENABLE_COVERAGE)
        set_tests_properties(umbf-convert_${FILENAME} PROPERTIES ENVIRONMENT "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/tests/coverage/umbftool_${FILENAME}.profraw")
    endif()
endforeach()