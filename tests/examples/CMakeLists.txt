add_executable(ompl_rigid_body_example test_impl.cpp)

target_link_libraries(ompl_rigid_body_example PRIVATE ompl::ompl Boost::headers)

target_link_libraries(ompl_rigid_body_example PRIVATE "-fsanitize=undefined")