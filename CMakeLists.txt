cmake_minimum_required(VERSION 4.3)
project(foo)


set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


add_library(foo_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    target_compile_options(foo_warnings INTERFACE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
endif()


add_subdirectory(core)
add_subdirectory(main)
add_subdirectory(tests)