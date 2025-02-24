cmake_minimum_required(VERSION 3.17)
project(assettool
    VERSION 0.0.1
    LANGUAGES CXX C
    DESCRIPTION "App3d Asset Tool")

add_files(assettool RECURSE "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(assettool ${SOURCE_FILES})

target_link_libraries(assettool PRIVATE
    app3d-core
    app3d-assets
    app3d-ecl
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

if(WIN32)
    # Manifest
    gen_manifest_app(${PROJECT_NAME} ${PROJECT_VERSION}
        app3d-core:${app3d-core_VERSION}
        app3d-assets:${app3d-assets_VERSION}
        app3d-ecl:${app3d-ecl_VERSION}
        vma:${vma_VERSION}
    )
    # Config
    gen_app_config()
endif()