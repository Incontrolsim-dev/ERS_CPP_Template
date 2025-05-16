
include(${ers_engine_SOURCE_DIR}/ERS/LinkEngine.cmake)

add_executable(a_wealth_of_rows a_wealth_of_rows.cpp)
set_property(TARGET a_wealth_of_rows PROPERTY CXX_STANDARD 20)
target_include_directories(a_wealth_of_rows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET a_wealth_of_rows PROPERTY FOLDER "Examples/C++")

ERS_Link(a_wealth_of_rows)
ERS_Add_Test(a_wealth_of_rows)