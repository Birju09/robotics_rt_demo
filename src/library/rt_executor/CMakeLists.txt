add_library(rt_executor STATIC
    rt_executor.cpp
)

target_include_directories(rt_executor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(rt_executor PRIVATE
    trajectory
)
