cmake_minimum_required(VERSION 3.24)
file(READ VERSION version_from_file)
project(rdftools
        VERSION ${version_from_file})

include(${PROJECT_SOURCE_DIR}/cmake/boilerplate_init.cmake)

boilerplate_init()

if (CMAKE_CXX_COMPILER_ID MATCHES " Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fomit-frame-pointer -momit-leaf-frame-pointer")
else ()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fomit-frame-pointer")
endif ()

if (DEFINED ${MARCH})
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=${MARCH} -mtune=${MARCH}")
endif ()

option(STATIC "Link binaries fully statically." OFF)
if (STATIC)
    message(STATUS "Binaries are linked fully statically.")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--eh-frame-hdr -static")
    SET(CMAKE_FIND_LIBRARY_SUFFIXES .a)
endif ()

option(WITH_TCMALLOC "Link and use tcmalloc." OFF)
if (WITH_TCMALLOC)
    find_library(TCMALLOCMINIMAL tcmalloc_minimal)
    if (NOT TCMALLOCMINIMAL)
        find_library(TCMALLOCMINIMAL tcmalloc-minimal)
    endif ()
    if (NOT TCMALLOCMINIMAL)
        message(FATAL_ERROR "Neither tcmalloc-minimal nor tcmalloc_minimal was found")
    endif ()
    message(STATUS "Linking against tcmalloc minimal ${TCMALLOCMINIMAL}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${TCMALLOCMINIMAL}")
endif ()


# set library options
include(${PROJECT_SOURCE_DIR}/cmake/conan_cmake.cmake)
install_packages_via_conan("${PROJECT_SOURCE_DIR}/conanfile.txt" "")

add_subdirectory(execs)
