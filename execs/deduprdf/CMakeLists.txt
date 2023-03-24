cmake_minimum_required(VERSION 3.21)

set(serd_source_files
        include/serd/serd.h
        src/attributes.h
        src/base64.c
        src/base64.h
        src/byte_sink.h
        src/byte_source.c
        src/byte_source.h
        src/env.c
        src/n3.c
        src/node.c
        src/node.h
        src/reader.c
        src/reader.h
        src/serd_config.h
        src/serd_internal.h
        src/stack.h
        src/string.c
        src/string_utils.h
        src/system.c
        src/system.h
        src/uri.c
        src/uri_utils.h
        src/writer.c
        )

foreach(serd_source_file ${serd_source_files})
    file(DOWNLOAD "https://raw.githubusercontent.com/dice-group/serd/95f5929c06a85495513fceee568a08c3cafaae83/${serd_source_file}"
            "${CMAKE_CURRENT_BINARY_DIR}/serd/${serd_source_file}"
            TLS_VERIFY ON)
endforeach()
file(DOWNLOAD "https://raw.githubusercontent.com/dice-group/serd/36cd3b34bd7e0793b13aa37b1a3d7e67854c4807/COPYING"
        "${CMAKE_CURRENT_BINARY_DIR}/serd/COPYING"
        TLS_VERIFY ON)

list(FILTER serd_source_files INCLUDE REGEX "^.+\\.c$")
list(TRANSFORM serd_source_files PREPEND "${CMAKE_CURRENT_BINARY_DIR}/serd/")


# get the name of the current folder as name for the executable
get_filename_component(exec_name ${CMAKE_CURRENT_LIST_DIR} NAME)

configure_file(${PROJECT_SOURCE_DIR}/cmake/version.hpp.in ${CMAKE_CURRENT_SOURCE_DIR}/src/rdftools_version.hpp)

find_package(rdf4cpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(cxxopts REQUIRED)
find_package(xxHash REQUIRED)

add_executable(${exec_name}
        src/main.cpp
        ${serd_source_files}
        src/parser/IStreamQuadIteratorSerdImpl.cpp src/parser/IStreamQuadIterator.cpp)

target_include_directories(${exec_name}
        PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/serd/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/serd/src>"
)

target_link_libraries(${exec_name} PRIVATE
        rdf4cpp::rdf4cpp
        spdlog::spdlog
        cxxopts::cxxopts
        xxHash::xxHash
        )

set_target_properties(${exec_name} PROPERTIES
        VERSION ${PROJECT_VERSION}
        CXX_STANDARD 20
        CXX_EXTENSIONS OFF
        CXX_STANDARD_REQUIRED ON
        )

include(${PROJECT_SOURCE_DIR}/cmake/execs_optimizations.cmake)
execs_optimizations(${exec_name})
