cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME car)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    stm32cubemx

    # Add user defined libraries
)

# Generate common firmware images after every successful build.
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating HEX and BIN firmware images"
)

# The path can be overridden from a CMake preset or with
# -DSTM32_PROGRAMMER_CLI=<path-to-STM32_Programmer_CLI.exe>.
find_program(STM32_PROGRAMMER_CLI
    NAMES STM32_Programmer_CLI STM32_Programmer_CLI.exe
    DOC "STM32CubeProgrammer command-line executable"
)

if(STM32_PROGRAMMER_CLI)
    add_custom_target(flash
        COMMAND "${STM32_PROGRAMMER_CLI}"
                -c port=SWD mode=UR reset=HWrst
                -w $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                -v
                -rst
        DEPENDS ${CMAKE_PROJECT_NAME}
        USES_TERMINAL
        COMMENT "Flashing ${CMAKE_PROJECT_NAME} through ST-Link/SWD"
    )
else()
    message(WARNING
        "STM32CubeProgrammer CLI was not found; the 'flash' target is unavailable."
    )
endif()

# Optional OpenOCD flash target for ST-Link/SWD. The executable and config can
# be overridden from a CMake preset or the command line.
set(OPENOCD_EXECUTABLE "" CACHE FILEPATH "OpenOCD executable")
if(OPENOCD_EXECUTABLE AND EXISTS "${OPENOCD_EXECUTABLE}")
    add_custom_target(flash-openocd
        COMMAND "${OPENOCD_EXECUTABLE}"
                -f "${CMAKE_SOURCE_DIR}/openocd_stm32f407.cfg"
                -c "program $<TARGET_FILE:${CMAKE_PROJECT_NAME}> verify reset exit"
        DEPENDS ${CMAKE_PROJECT_NAME}
        USES_TERMINAL
        COMMENT "Flashing ${CMAKE_PROJECT_NAME} through OpenOCD/ST-Link"
    )
else()
    message(STATUS "OpenOCD flash target disabled: set OPENOCD_EXECUTABLE to openocd.exe")
endif()
