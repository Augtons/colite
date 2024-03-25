cmake_minimum_required(VERSION 3.16)
project(colite)

set(CMAKE_CXX_STANDARD 20)
set(COLITE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

file(GLOB_RECURSE LIB_SRCS "${COLITE_DIR}/src/*.cpp")
add_library(colite STATIC ${LIB_SRCS})
target_include_directories(colite PUBLIC "${COLITE_DIR}/src")

file(GLOB_RECURSE EXAMPLE_SRCS "${COLITE_DIR}/example/*.cpp")
add_executable(example ${EXAMPLE_SRCS})
target_link_libraries(example PUBLIC colite)