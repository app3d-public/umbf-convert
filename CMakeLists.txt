cmake_minimum_required(VERSION 3.17)

if(NOT DEFINED ACBT_PROJECT_LOADED)
    include(cmake/project.cmake)
endif()

project(umbf-convert
    VERSION 0.0.1
    LANGUAGES CXX C
    DESCRIPTION "UMBF Convert")

if(NOT DEFINED ACBT_LOADED)
    include(cmake/utils.cmake)
endif()

if(NOT TARGET acul)
    add_subdirectory(modules/acul)
endif()

if(NOT TARGET umbf)
    add_subdirectory(modules/umbf)
endif()

if(NOT TARGET aecl)
    add_subdirectory(modules/aecl)
endif()

add_subdirectory(modules/3rd-party/args)

add_files(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/src" RECURSE)
add_executable(${PROJECT_NAME} ${UMBF_CONVERT_SRC})

target_link_libraries(${PROJECT_NAME} PRIVATE
    acul
    aecl
    umbf
    args
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

# if(WIN32)
#     # Manifest
#     gen_manifest_app(${PROJECT_NAME} ${PROJECT_VERSION}
#         libacul:${acul_VERSION}
#         libumbf:${umbf_VERSION}
#         libaecl:${aecl_VERSION}
#     )

#     # Config
#     gen_app_config()
# endif()