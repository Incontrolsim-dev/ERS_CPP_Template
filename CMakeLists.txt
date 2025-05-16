cmake_minimum_required (VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(DOWNLOAD "https://gist.githubusercontent.com/markoostveen/37c2113fc0fddb86ed62e7b8495499f1/raw/449eef1e70fe3941cd904f956a58639c9e4f74fc/gistfile1.txt"
"${CMAKE_BINARY_DIR}/setup.cmake"
STATUS download_status)
include(${CMAKE_BINARY_DIR}/setup.cmake)
ERS_Download_SDK()

#Configure name
project(ERS_Template VERSION 0.1)
ERS_Enable_Additional_Build_Settings()
ERS_RUN_VCPKG()
ERS_Setup_SDK()

# Add command line arguments for specific compilers
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fPIC)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-fPIC)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
endif()

add_subdirectory(CppExample)