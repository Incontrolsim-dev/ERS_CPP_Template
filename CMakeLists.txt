cmake_minimum_required (VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(ERS_Template VERSION 0.1)

set(ERS_VERSION "0.7.0")
set(ZIP_URL "https://github.com/Incontrolsim-dev/ERS_Binaries/releases/download/${ERS_VERSION}/ERS_CPP.zip")
set(ZIP_FILE "${CMAKE_BINARY_DIR}/ERS_CPP.zip")
set(EXTRACT_DIR "${CMAKE_BINARY_DIR}/ers_cpp")

if(NOT EXISTS "${ZIP_FILE}")
    # Download
    file(DOWNLOAD
        "${ZIP_URL}"
        "${ZIP_FILE}"
        SHOW_PROGRESS
    )

    # Extract
    file(ARCHIVE_EXTRACT
        INPUT "${ZIP_FILE}"
        DESTINATION "${EXTRACT_DIR}"
    )
endif()


# Add command line arguments for specific compilers
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fPIC)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-fPIC)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
endif()

# Create ERS target
file(GLOB_RECURSE ERS_SDK_SOURCES
    "${EXTRACT_DIR}/SDK/Include/*.cpp"
    "${EXTRACT_DIR}/SDK/Include_Minimal/*.cpp"
)
add_library(ers STATIC ${ERS_SDK_SOURCES})
set_property(TARGET ers PROPERTY CXX_STANDARD 20)
if (MSVC)
	target_compile_options(ers PUBLIC /Zc:preprocessor)
endif()
target_include_directories(ers SYSTEM PUBLIC
    "${EXTRACT_DIR}/SDK/Include/Common"
    "${EXTRACT_DIR}/SDK/Include/Ers"
    "${EXTRACT_DIR}/SDK/Include_Minimal"
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_link_libraries(ers PUBLIC "${EXTRACT_DIR}/bin/GNU/libers-engine.so")
    target_link_libraries(ers PUBLIC atomic)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	target_link_libraries(ers PUBLIC "${EXTRACT_DIR}/bin/Clang/libers-engine.so")
    target_link_libraries(ers PUBLIC atomic)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	target_link_libraries(ers PUBLIC "${EXTRACT_DIR}/lib/MSVC/ers-engine.lib")
endif()

# Helper function to automatically copy ers-engine DLL or so file to build directory
function(ERS_copy_dll_so target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${EXTRACT_DIR}/bin/${CMAKE_CXX_COMPILER_ID}"
        $<TARGET_FILE_DIR:${target}>
    )
endfunction()

add_subdirectory(CppExample)
