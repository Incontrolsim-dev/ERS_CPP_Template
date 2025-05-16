include(${ers_engine_SOURCE_DIR}/ERS/LinkEngine.cmake)

add_executable(mover_model_sync mover_model_sync.cpp)
set_property(TARGET mover_model_sync PROPERTY CXX_STANDARD 20)
target_include_directories(mover_model_sync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET mover_model_sync PROPERTY FOLDER "Examples/C++")

ERS_Link(mover_model_sync)
ERS_Add_Test(mover_model_sync)
