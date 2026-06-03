add_library(coal_gpu_collision_manager STATIC
    coal_gpu_collision_manager.cpp
)

target_include_directories(coal_gpu_collision_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)


target_link_libraries(coal_gpu_collision_manager PUBLIC
    coal::coal
    OpenCL::OpenCL
)