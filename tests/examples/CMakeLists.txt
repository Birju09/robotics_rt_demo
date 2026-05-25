add_executable(ompl_rigid_body_example test_impl.cpp)

target_link_libraries(ompl_rigid_body_example PRIVATE ompl::ompl Boost::headers)

target_compile_options(ompl_rigid_body_example PRIVATE "-fsanitize=undefined")
target_link_libraries(ompl_rigid_body_example PRIVATE "-fsanitize=undefined")

target_compile_options(ompl_rigid_body_example PRIVATE
    -fpass-plugin=$<TARGET_FILE:UBSanCountPlugin>)

add_dependencies(ompl_rigid_body_example UBSanCountPlugin)
