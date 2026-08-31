set(UMBFTOOL_INPUT "data")
set(UMBFTOOL_INPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/data/input")
set(UMBFTOOL_OUTPUT_BUILD "${CMAKE_CURRENT_BINARY_DIR}/data/output")

file(MAKE_DIRECTORY ${UMBFTOOL_OUTPUT_BUILD})
file(MAKE_DIRECTORY ${UMBFTOOL_INPUT_BUILD})

add_executable(umbf-block-iterator umbf_block_iterator.cpp)
target_link_libraries(umbf-block-iterator PRIVATE umbf)
add_test(NAME umbf-block-iterator
    COMMAND $<TARGET_FILE:umbf-block-iterator> ${UMBFTOOL_OUTPUT_BUILD}/block-iterator.umbf
)
set_tests_properties(umbf-block-iterator PROPERTIES LABELS "umbftool")
add_executable(umbf-mapping umbf_mapping.cpp)
target_link_libraries(umbf-mapping PRIVATE umbf)
add_test(NAME umbf-mapping COMMAND $<TARGET_FILE:umbf-mapping> ${UMBFTOOL_OUTPUT_BUILD}/mapping.umbf)
set_tests_properties(umbf-mapping PROPERTIES LABELS "umbftool")
add_executable(umbf-block-tag umbf_block_tag.cpp)
target_link_libraries(umbf-block-tag PRIVATE umbf)
add_test(NAME umbf-block-tag COMMAND $<TARGET_FILE:umbf-block-tag> ${UMBFTOOL_OUTPUT_BUILD}/block-tag.umbf)
set_tests_properties(umbf-block-tag PROPERTIES LABELS "umbftool")
add_test(NAME umbf-show-checksum COMMAND $<TARGET_FILE:umbf-convert> show
    -i ${UMBFTOOL_OUTPUT_BUILD}/block-iterator.umbf)
set_tests_properties(umbf-show-checksum PROPERTIES DEPENDS umbf-block-iterator
    PASS_REGULAR_EXPRESSION "checksum: [0-9]+" LABELS "umbftool")
configure_file(${UMBFTOOL_INPUT}/test_image.png ${UMBFTOOL_INPUT_BUILD}/test_image.png COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/modules/aecl/tests/data/cube.obj ${UMBFTOOL_INPUT_BUILD}/cube.obj COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/modules/aecl/tests/data/cube.mtl ${UMBFTOOL_INPUT_BUILD}/cube.mtl COPYONLY)

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

add_test(NAME umbf-show_material_segments
    COMMAND $<TARGET_FILE:umbf-convert>
    show
    -i ${UMBFTOOL_OUTPUT_BUILD}/material_embedded.umbf
)
set_tests_properties(umbf-show_material_segments PROPERTIES
    DEPENDS umbf-convert_material_embedded
    LABELS "umbftool"
    PASS_REGULAR_EXPRESSION "textures size: 1"
)

add_test(NAME umbf-show_scene_segments
    COMMAND $<TARGET_FILE:umbf-convert>
    show
    -i ${UMBFTOOL_OUTPUT_BUILD}/scene_embedded.umbf
)
set_tests_properties(umbf-show_scene_segments PROPERTIES
    DEPENDS umbf-convert_scene_embedded
    LABELS "umbftool"
    PASS_REGULAR_EXPRESSION "object segments: 1"
)

add_test(NAME umbf-extract_scene_segments
    COMMAND $<TARGET_FILE:umbf-convert>
    extract
    -i ${UMBFTOOL_OUTPUT_BUILD}/scene_embedded.umbf
    -o ${UMBFTOOL_OUTPUT_BUILD}/scene_roundtrip.obj
)
set_tests_properties(umbf-extract_scene_segments PROPERTIES
    DEPENDS umbf-convert_scene_embedded
    LABELS "umbftool"
)

add_executable(umbf-object-segment umbf_object_segment.cpp)
target_link_libraries(umbf-object-segment PRIVATE umbf)
foreach(SOURCE_FORMAT scene json)
    if(SOURCE_FORMAT STREQUAL "scene")
        set(OBJECT_INPUT "${UMBFTOOL_INPUT_BUILD}/cube.obj")
    else()
        set(OBJECT_INPUT "${UMBFTOOL_INPUT_BUILD}/scene_meshonly.json")
    endif()
    foreach(CODEC raw zstd)
        set(COMPRESSION_ARGS)
        if(CODEC STREQUAL "zstd")
            set(COMPRESSION_ARGS --compressed)
        endif()
        set(OBJECT_CASE "object_${SOURCE_FORMAT}_${CODEC}")
        add_test(NAME umbf-convert_${OBJECT_CASE}
            COMMAND $<TARGET_FILE:umbf-convert> convert --format=${SOURCE_FORMAT}
                -i ${OBJECT_INPUT} -o ${UMBFTOOL_OUTPUT_BUILD}/${OBJECT_CASE}.umbf ${COMPRESSION_ARGS})
        add_test(NAME umbf-check_${OBJECT_CASE}
            COMMAND $<TARGET_FILE:umbf-object-segment> ${UMBFTOOL_OUTPUT_BUILD}/${OBJECT_CASE}.umbf ${CODEC})
        set_tests_properties(umbf-convert_${OBJECT_CASE} PROPERTIES LABELS "umbftool"
            FIXTURES_SETUP ${OBJECT_CASE})
        set_tests_properties(umbf-check_${OBJECT_CASE} PROPERTIES LABELS "umbftool"
            FIXTURES_REQUIRED ${OBJECT_CASE})
    endforeach()
endforeach()

# The JSON models retain source strings; conversion resolves paths at the point of use.
add_executable(umbf-json-paths json_paths.cpp ../src/models/jsonbase.cpp ../src/models/umbf.cpp)
target_include_directories(umbf-json-paths PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(umbf-json-paths PRIVATE aecl umbf)
add_test(NAME umbf-json-paths COMMAND $<TARGET_FILE:umbf-json-paths>
    ${UMBFTOOL_INPUT_BUILD}/texture.json)
set_tests_properties(umbf-json-paths PROPERTIES LABELS "umbftool")

# Keep descriptors apart from their resources and launch from a third directory.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/data/descriptors")
foreach(FILENAME texture scene_embedded library_embedded)
    configure_file(${UMBFTOOL_INPUT}/${FILENAME}.json.in
        ${CMAKE_CURRENT_BINARY_DIR}/data/descriptors/${FILENAME}.json @ONLY)
    add_test(NAME umbf-work-dir_${FILENAME}
        COMMAND $<TARGET_FILE:umbf-convert> convert --format=json
            -i data/descriptors/${FILENAME}.json
            -o data/output/work_dir_${FILENAME}.umbf
            --work-dir data/input)
    set_tests_properties(umbf-work-dir_${FILENAME} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" LABELS "umbftool")
endforeach()
add_test(NAME umbf-work-dir_absolute
    COMMAND $<TARGET_FILE:umbf-convert> convert --format=json
        -i ${CMAKE_CURRENT_BINARY_DIR}/data/descriptors/texture.json
        -o ${UMBFTOOL_OUTPUT_BUILD}/work_dir_absolute.umbf
        --work-dir ${UMBFTOOL_INPUT_BUILD})
set_tests_properties(umbf-work-dir_absolute PROPERTIES LABELS "umbftool")
