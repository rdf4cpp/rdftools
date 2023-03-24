cmake_minimum_required(VERSION 3.21)

# get the name of the current folder as name for the executable
get_filename_component(exec_name ${CMAKE_CURRENT_LIST_DIR} NAME)

configure_file(${PROJECT_SOURCE_DIR}/cmake/version.hpp.in ${CMAKE_CURRENT_SOURCE_DIR}/src/rdftools_version.hpp)


add_executable(${exec_name}
        src/main.cpp
        )

find_package(rdf4cpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(cxxopts REQUIRED)
find_package(xxHash REQUIRED)

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
