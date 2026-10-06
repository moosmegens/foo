file(GLOB TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(tests ${TEST_SOURCES})

target_link_libraries(tests
        PRIVATE
        foo_core
)

target_include_directories(tests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(
        NAME tests
        COMMAND tests
)