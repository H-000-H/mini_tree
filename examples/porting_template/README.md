# Porting Template for mini_tree

This directory contains stub implementations of the HAL (Hardware Abstraction Layer)
functions that `mini_tree` expects. Use this as a starting point when porting the
framework to a new MCU platform.

## How to Port

1. **Copy** this directory into your project (e.g., `my_project/hal_port/`).

2. **Implement** each `.c` file with your MCU's SDK calls:
   - `hal_cpu.c` — cycle counter, busy-wait
   - `hal_flash.c` — flash read for bit-rot scrubber
   - `hal_force_stop.c` — emergency peripheral shutdown
   - `hal_storage.c` — persistent config storage (A/B slots)
   - `hal_wdt.c` — RTC watchdog + task watchdog
   - `hal_platform_safety.c` — fault LED/buzzer hardware lock

3. **Rename** the library in `CMakeLists.txt` from `your_hal_port` to your
   actual library name.

4. **Update your project's CMakeLists.txt**:

   ```cmake
   add_subdirectory(path/to/mini_tree)
   add_subdirectory(path/to/hal_port)    # your HAL implementation
   target_link_libraries(my_app PRIVATE mini_tree hal_port)
   ```

5. **Provide FreeRTOSConfig.h** in your project's include path.

6. **Write your main.c**:

   ```c
   #include "system_init.hpp"
   
   int main(void) {
       platform_init();                // your MCU HAL init
       MiniTree::System_Pre_OS_Init(); // framework phase 1
       register_my_drivers();          // register your hal_* with VFS
       MiniTree::System_Start_Tasks(); // framework phase 2
       my_app_init();                  // your app tasks
       vTaskStartScheduler();
   }
   ```

## ESP32 Quick Start

If you're targeting ESP32, the complete HAL implementation is maintained
separately at: `https://github.com/your-org/esp32-hal-port` (or similar).
