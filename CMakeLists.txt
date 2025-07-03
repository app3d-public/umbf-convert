cmake_minimum_required(VERSION 3.17)
project(umbftool
    VERSION 0.0.1
    LANGUAGES CXX C
    DESCRIPTION "UMBF Tool")

add_files(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}" RECURSE)
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    acul
    umbf
    aecl
)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/tools"
)

if(WIN32)
    # Manifest
    gen_manifest_app(${PROJECT_NAME} ${PROJECT_VERSION}
        acul:${acul_VERSION}
        umbf:${umbf_VERSION}
        aecl:${aecl_VERSION}
        vma:${vma_VERSION}
    )
    # Config
    gen_app_config()
endif()