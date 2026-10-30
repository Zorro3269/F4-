cmake_minimum_required(VERSION 3.22)
enable_language(C ASM)

set(MX_Include_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/SYSTEM/sys
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/SYSTEM/delay
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/SYSTEM/usart
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/LCD
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/TOUCH
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/IIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/24CXX
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/FreeRTOS/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/FreeRTOS/portable/GCC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/LVGL
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/LVGL/src
)

set(MX_Defines_Syms USE_HAL_DRIVER STM32F407xx LV_CONF_INCLUDE_SIMPLE)

file(GLOB HAL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_can.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sram.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_fsmc.c
)

file(GLOB FREERTOS_CORE CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/FreeRTOS/*.c)
file(GLOB LVGL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/LVGL/src/*.c)
file(GLOB_RECURSE LVGL_SUBSOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/LVGL/src/**/*.c)
file(GLOB_RECURSE BSP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/LCD/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/TOUCH/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/IIC/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/BSP/24CXX/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/SYSTEM/sys/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/SYSTEM/delay/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/SYSTEM/usart/*.c
)
list(FILTER BSP_SOURCES EXCLUDE REGEX "/lcd_ex\\.c$")
list(FILTER BSP_SOURCES EXCLUDE REGEX "/usart\\.c$")

set(MX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/system_stm32f4xx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32f4xx_hal_msp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32f4xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/startup_stm32f407xx.c
    ${HAL_SOURCES} ${FREERTOS_CORE} ${LVGL_SOURCES} ${LVGL_SUBSOURCES} ${BSP_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/FreeRTOS/portable/GCC/port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/FreeRTOS/portable/MemMang/heap_4.c
)

add_library(stm32cubemx INTERFACE)
target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${MX_SOURCES})
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE m)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES ADDITIONAL_CLEAN_FILES "${CMAKE_PROJECT_NAME}.map")
