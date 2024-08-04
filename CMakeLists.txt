cmake_minimum_required(VERSION 3.17)
project(assettool
    VERSION 0.0.1
    LANGUAGES CXX C
    DESCRIPTION "App3d Asset Tool")

add_files(assettool RECURSE "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(assettool ${SOURCE_FILES})

target_include_directories(assettool PRIVATE $<TARGET_PROPERTY:app3d-core,INCLUDE_DIRECTORIES>)
target_include_directories(assettool PRIVATE $<TARGET_PROPERTY:app3d-assets,INCLUDE_DIRECTORIES>)
target_include_directories(assettool PRIVATE $<TARGET_PROPERTY:app3d-ecl,INCLUDE_DIRECTORIES>)

target_link_libraries(assettool PRIVATE
    app3d-core
    app3d-assets
    app3d-ecl
    ${OPENIMAGEIO_LIBRARIES}
)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

set_target_properties(assettool
    PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/tools"
)