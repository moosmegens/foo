add_executable(foo_main main.cpp)
target_link_libraries(foo_main PRIVATE foo_core)

target_compile_definitions(foo_main PRIVATE
        DEV__FOO_ROOT="${PROJECT_SOURCE_DIR}")