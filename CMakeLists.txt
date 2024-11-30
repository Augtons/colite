cmake_minimum_required(VERSION 3.16)
project(colite)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(COLITE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

if(WIN32)
  message(STATUS "Select Platform `Windows`")
  set(COLITE_PLATFORM "Windows")
endif()

if(NOT DEFINED COLITE_PORT_INCLUDE_DIR)
  set(COLITE_PORT_INCLUDE_DIR "${COLITE_DIR}/Port/${COLITE_PLATFORM}")
endif()

# Library
file(GLOB_RECURSE LIB_SRCS "${COLITE_DIR}/Src/*.cpp")
list(LENGTH LIB_SRCS LIB_SRCS_LEN)
if(LIB_SRCS_LEN GREATER 0)
  add_library(colite STATIC ${LIB_SRCS})
  target_include_directories(colite PUBLIC "${COLITE_DIR}/Src/include"
                                           "${COLITE_PORT_INCLUDE_DIR}")
else()
  add_library(colite INTERFACE)
  target_include_directories(colite INTERFACE "${COLITE_DIR}/Src/include"
                                              "${COLITE_PORT_INCLUDE_DIR}")
endif()
add_library(colite::colite ALIAS colite)
unset(LIB_SRCS)
unset(LIB_SRCS_LEN)

# Examples
file(GLOB COLITE_EXAMPLES LIST_DIRECTORIES true "${COLITE_DIR}/Examples/*")
foreach(DIR IN LISTS COLITE_EXAMPLES)
  if(NOT IS_DIRECTORY ${DIR})
    continue()
  endif()
  set(EXAMPLE_CMAKE_LIST_PATH "${DIR}/CMakeLists.txt")
  if(EXISTS ${EXAMPLE_CMAKE_LIST_PATH})
    message(STATUS "Add Example `${DIR}`")
    add_subdirectory(${DIR})
  endif()
endforeach()
unset(COLITE_EXAMPLES)
