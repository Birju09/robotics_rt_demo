add_library(trajectory STATIC
    trajectory.cpp
)

target_include_directories(trajectory PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
