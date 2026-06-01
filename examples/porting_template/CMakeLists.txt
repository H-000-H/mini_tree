# ==============================================================================
# examples/porting_template/CMakeLists.txt
#
# Porting template: copy this directory to your project, implement the stubs,
# then add_subdirectory() in your project's CMakeLists.txt.
#
# Your project's CMakeLists.txt should look like:
#
#   add_subdirectory(path/to/mini_tree)
#   add_subdirectory(path/to/your_hal_port)
#   target_link_libraries(my_app PRIVATE mini_tree your_hal_port)
# ==============================================================================

# This library provides the hal_* symbols that mini_tree expects.
# Each function is declared in hal_if/include/*.h — implement them
# using your MCU's SDK (STM32 HAL, ESP-IDF, NXP MCUXpresso, etc.).
add_library(your_hal_port STATIC
    hal_cpu.c
    hal_flash.c
    hal_force_stop.c
    hal_storage.c
    hal_wdt.c
    hal_platform_safety.c
)

target_include_directories(your_hal_port
    PRIVATE
        # Add your MCU SDK include paths here
        # ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc
)

target_link_libraries(your_hal_port
    PUBLIC
        hal_if              # hal_* types and declarations
    PRIVATE
        # Add your MCU SDK libraries here
)
