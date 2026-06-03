
add_executable(panda_demo main.cpp)

target_link_libraries(panda_demo PRIVATE 
    collision_detection 
    motion_planning 
    robot_model 
    rt_executor
    orocos-kdl::orocos-kdl
)

install(TARGETS panda_demo
    RUNTIME DESTINATION bin
)
