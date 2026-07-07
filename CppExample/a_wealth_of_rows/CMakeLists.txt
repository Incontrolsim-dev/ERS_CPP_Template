project(a_wealth_of_rows)
add_executable(${PROJECT_NAME} a_wealth_of_rows.cpp ${ERS_SDK_SOURCES})
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "Examples/C++")

target_link_libraries(${PROJECT_NAME} PRIVATE ers)
ERS_copy_dll_so(${PROJECT_NAME})
