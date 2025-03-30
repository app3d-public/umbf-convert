set(UMBFTOOL_INPUT "data")
set(UMBFTOOL_INPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/data/input")
set(UMBFTOOL_OUTPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/data/output")

file(MAKE_DIRECTORY ${ASSETTOOL_OUTPUT_BUILD})

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

    add_test(NAME umbftool_${FILENAME}
        COMMAND $<TARGET_FILE:umbftool>
                convert
                -i ${UMBFTOOL_INPUT_BUILD}/${FILENAME}.json 
                -o ${UMBFTOOL_OUTPUT_BUILD}/${FILENAME}.umbf
                --format=json
                )
    set_tests_properties(umbftool_${FILENAME} PROPERTIES LABELS "umbftool")
endforeach()