project(luma_usd)

cmake_minimum_required(VERSION 3.1)

list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_SOURCE_DIR}/cmake")

find_package(USD REQUIRED)

set(CMAKE_CXX_STANDARD 11)

# TODO: Windows support.
add_compile_options(-Wall -DBUILD_OPTLEVEL_OPT -DBUILD_COMPONENT_SRC_PREFIX="")
option(BUILD_MYSQL_RESOLVER "Build the MySQL URI resolver")

if (BUILD_MYSQL_RESOLVER)
    set(EXTERNAL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)

    set(Z85_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/external/z85/z85_impl.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/external/z85/z85.c)

    add_subdirectory(external)
    add_subdirectory(MySQLResolver)
endif ()

option(BUILD_S3_RESOLVER "Build the S3 URI resolver" On)

if (BUILD_S3_RESOLVER)
    add_subdirectory(S3Resolver)
endif()

install(
    FILES plugInfo.json
    DESTINATION .)
