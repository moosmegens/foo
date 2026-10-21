add_executable(foo_cli main.cpp)
target_link_libraries(foo_cli PRIVATE foo_core)