# STM32F407ZG OBD2 Wi-Fi Bridge

This project targets the STM32F407ZG only. It combines OBD-II/CAN data acquisition, an LVGL display, and ESP8266-based Wi-Fi management.

## Build and flash

Open this directory in CLion and allow CMake to reload the project. The `Debug` and `Release` CMake presets use the ARM GNU toolchain configured in `cmake/gcc-arm-none-eabi.cmake` and generate `car.elf`, `car.hex`, and `car.bin`.

To program the board, connect an ST-Link to SWDIO, SWCLK, GND, and target power as required by the board. Select the `flash` CMake target in CLion and run it. The target uses STM32CubeProgrammer over SWD, verifies the image, and resets the MCU. Hardware must be connected for an actual flash operation.

## Firmware components

- STM32F407ZG startup, CMSIS, and STM32F4 HAL
- FreeRTOS
- LVGL display and touch support
- CAN/OBD-II acquisition and six-value data frame
- ESP8266 AT-based Wi-Fi interface

F1-specific startup files, linker scripts, CubeMX metadata, and drivers are not part of this project.
