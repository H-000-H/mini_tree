# mini_tree Rust crate 集成 — 通过 cargo 构建 staticlib 并导入 CMake 目标
#
# 用法:
#   include("${CMAKE_CURRENT_LIST_DIR}/cmake/rust.cmake")
#   mini_tree_add_rust_crate(time_slice "${CMAKE_CURRENT_LIST_DIR}/time_slice")
#   target_link_libraries(mini_tree PRIVATE time_slice_rust)

find_program(CARGO_EXECUTABLE cargo REQUIRED)

function(mini_tree_add_rust_crate CRATE_NAME CRATE_DIR)
    if(NOT IS_ABSOLUTE "${CRATE_DIR}")
        set(CRATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${CRATE_DIR}")
    endif()

    set(_rust_target "thumbv7em-none-eabihf")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_rust_profile "debug")
        set(_cargo_profile_args "")
    else()
        set(_rust_profile "release")
        set(_cargo_profile_args "--release")
    endif()

    set(_cargo_target_dir "${CMAKE_BINARY_DIR}/rust/${CRATE_NAME}")
    set(_static_lib "${_cargo_target_dir}/${_rust_target}/${_rust_profile}/lib${CRATE_NAME}.a")

    file(GLOB_RECURSE _rust_sources CONFIGURE_DEPENDS
         "${CRATE_DIR}/src/*.rs"
         "${CRATE_DIR}/Cargo.toml"
         "${CRATE_DIR}/.cargo/config.toml")

    add_custom_command(
        OUTPUT "${_static_lib}"
        COMMAND "${CMAKE_COMMAND}" -E env
                "CARGO_TARGET_DIR=${_cargo_target_dir}"
                "${CARGO_EXECUTABLE}" build
                --manifest-path "${CRATE_DIR}/Cargo.toml"
                --target "${_rust_target}"
                ${_cargo_profile_args}
        DEPENDS ${_rust_sources}
        WORKING_DIRECTORY "${CRATE_DIR}"
        COMMENT "Building Rust crate ${CRATE_NAME} (${_rust_profile})"
        VERBATIM
    )

    set(_import_target "${CRATE_NAME}_rust")
    add_library(${_import_target} STATIC IMPORTED GLOBAL)
    set_target_properties(${_import_target} PROPERTIES
        IMPORTED_LOCATION "${_static_lib}"
    )
    add_custom_target(${CRATE_NAME}_rust_build DEPENDS "${_static_lib}")
    add_dependencies(${_import_target} ${CRATE_NAME}_rust_build)
endfunction()
