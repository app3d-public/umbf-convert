set(ASSETTOOL_INPUT "data")
set(ASSETTOOL_INPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/data/input")
set(ASSETTOOL_OUTPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/data/output")

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
    configure_file(${ASSETTOOL_INPUT}/${FILENAME}.json.in 
                   ${ASSETTOOL_INPUT_BUILD}/${FILENAME}.json 
                   @ONLY)

    add_test(NAME assettool_${FILENAME}
        COMMAND $<TARGET_FILE:assettool>
                -i ${ASSETTOOL_INPUT_BUILD}/${FILENAME}.json 
                -o ${ASSETTOOL_OUTPUT_BUILD}/${FILENAME}.a3d 
                --json
                --check)
    set_tests_properties(assettool_${FILENAME} PROPERTIES LABELS "assettool")
endforeach()