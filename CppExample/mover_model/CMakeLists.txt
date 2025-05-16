include(${ers_engine_SOURCE_DIR}/ERS/LinkEngine.cmake)

add_executable(mover_model mover_model.cpp)
set_property(TARGET mover_model PROPERTY CXX_STANDARD 20)
target_include_directories(mover_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET mover_model PROPERTY FOLDER "Examples/C++")

ERS_Link(mover_model)
ERS_Add_Test(mover_model)
