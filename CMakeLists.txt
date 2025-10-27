
cmake_minimum_required(VERSION 3.24)

# Project settings
project(gdexport
        VERSION 0.1.0
        DESCRIPTION "Utility to support auto-generating interface for a Godot GDExtension"
        LANGUAGES C CXX
        HOMEPAGE_URL "https://www.gridshadows.co.uk")

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  MESSAGE(FATAL_ERROR "Can only be compiled with clang")
endif()

# Options:
#   Clang version
set(GDEXPORT_CLANG_VERSION "" CACHE STRING "Specifies the version of clang to link against")
if(GDEXPORT_CLANG_VERSION STREQUAL "")
  set(GDEXPORT_CLANG_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
endif()
#   godot-cpp path
set(GDEXPORT_GODOT_CPP "" CACHE FILEPATH "Specifies the path to godot-cpp")
if(GDEXPORT_GODOT_CPP STREQUAL "")
  set(GDEXPORT_GODOT_CPP "${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(
  "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fno-rtti;-Wall>"
  "$<$<AND:$<CXX_COMPILER_ID:GNU>,$<CONFIG:DEBUG>>:-fno-omit-frame-pointer>"
)

############## LLVM CONFIGURATION #################

# LLVM_DIR must be set to the prefix of /share/llvm/cmake via commandline
find_package(LLVM ${CMAKE_CXX_COMPILER_VERSION} REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# We incorporate the CMake features provided by LLVM:
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

# LLVM_DIR must be set to the prefix of /share/llvm/cmake via commandline
find_package(Clang ${CMAKE_CXX_COMPILER_VERSION} REQUIRED)

message("LLVM STATUS:
  Definitions ${LLVM_DEFINITIONS}
  Includes    ${LLVM_INCLUDE_DIRS}
              ${CLANG_INCLUDE_DIRS}
  Libraries   ${LLVM_LIBRARY_DIRS}"
)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# Now set the LLVM header and library paths:
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS}
  "${GDEXPORT_GODOT_CPP}/include"
  "${GDEXPORT_GODOT_CPP}/gen/include"
  "${GDEXPORT_GODOT_CPP}/gdextension")
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_library(gdexport MODULE
  gdexport.cpp
  attributes.cpp
  extractinterfacevisitor.cpp
  extractdocvisitor.cpp
  utilities.cpp
)

set_target_properties(gdexport PROPERTIES
  CXX_VISIBILITY_PRESET hidden
)

target_link_libraries(gdexport PRIVATE clang-cpp)
