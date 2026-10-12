# POST_BUILD disassembly listing when mini_tree .config has CONFIG_BUILD_DISASM=y

function(mini_tree_add_disasm_postbuild kconfig_dot target)
    if(NOT TARGET ${target})
        message(WARNING "mini_tree_add_disasm_postbuild: target '${target}' not found")
        return()
    endif()
    if(NOT EXISTS "${kconfig_dot}")
        return()
    endif()
    file(STRINGS "${kconfig_dot}" _disasm_entry REGEX "^CONFIG_BUILD_DISASM=y$")
    if(NOT _disasm_entry STREQUAL "CONFIG_BUILD_DISASM=y")
        return()
    endif()
    if(NOT CMAKE_OBJDUMP)
        find_program(_mini_tree_objdump NAMES objdump)
        if(_mini_tree_objdump)
            set(CMAKE_OBJDUMP "${_mini_tree_objdump}")
        endif()
    endif()
    if(NOT CMAKE_OBJDUMP)
        message(WARNING "CONFIG_BUILD_DISASM=y but CMAKE_OBJDUMP not found")
        return()
    endif()
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        WORKING_DIRECTORY $<TARGET_FILE_DIR:${target}>
        COMMAND ${CMAKE_OBJDUMP} -d -S $<TARGET_FILE_NAME:${target}>
                > $<TARGET_FILE_BASE_NAME:${target}>.lst
        COMMENT "Generating disassembly listing (.lst)"
    )
endfunction()
