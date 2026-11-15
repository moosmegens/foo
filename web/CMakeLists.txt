add_executable(foo_web
        main.cpp
)

target_link_libraries(foo_web PRIVATE foo_core)

if(EMSCRIPTEN)
    target_link_options(foo_web PRIVATE
            "--bind"
            "-sMODULARIZE=1"
            "-sEXPORT_ES6=1"
            "-sALLOW_MEMORY_GROWTH=1"
    )

    set_target_properties(foo_web PROPERTIES
            SUFFIX ".js"
    )

    add_custom_target(foo_web_files ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/static $<TARGET_FILE_DIR:foo_web>

            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/demo $<TARGET_FILE_DIR:foo_web>
    )
    add_dependencies(foo_web_files foo_web)
endif()