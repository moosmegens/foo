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

    configure_file(
            index.html
            ${CMAKE_BINARY_DIR}/index.html
            COPYONLY
    )
endif()