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

    add_custom_target(foo_web_html ALL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/index.html
            $<TARGET_FILE_DIR:foo_web>/index.html
    )
endif()