cmake_minimum_required (VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(DOWNLOAD "https://raw.githubusercontent.com/Incontrolsim-dev/ERS_CPP_SDK/main/RemoteSetup.cmake" "${CMAKE_CURRENT_BINARY_DIR}/RemoteSetup.cmake" STATUS download_status)
include(${CMAKE_CURRENT_BINARY_DIR}/RemoteSetup.cmake)
ERS_Download_SDK("0.6.0-feature-ers-editor-6315-8a58b595")

#Configure name
project(ERS_Template VERSION 0.1)
ERS_Enable_Additional_Build_Settings()
ERS_RUN_VCPKG()

# Add command line arguments for specific compilers
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fPIC)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-fPIC)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
endif()

add_subdirectory(CppExample)