cmake_minimum_required(VERSION 3.13...3.31)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# PicoBoot-WiFi is for the Pico 2 W only (design D1)
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK 2 (must be before project)
include(pico_sdk_2_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.3.0")
    message(FATAL_ERROR "PicoBoot-WiFi needs Pico SDK 2.3.0 or newer (found ${PICO_SDK_VERSION_STRING})")
endif()

project(picoboot-wifi
    LANGUAGES C CXX ASM
)

pico_sdk_init()

if(NOT PICO_BOARD STREQUAL "pico2_w")
    message(FATAL_ERROR "PicoBoot-WiFi is for the Pico 2 W only (PICO_BOARD=pico2_w), see design D1")
endif()

# Bench debugging: the firmware prints its start-up steps over USB serial (waits up to 10 s for a
# terminal). Not for the console.
option(PICOBOOT_WIFI_USB_TRACE "Print the firmware's start-up steps over USB serial" OFF)

# Clock and flash speed during the injection (design D27): 250 MHz with flash divider 3 (83 MHz),
# as tested in phase 1. A board that takes the overclock or fast flash badly can try lower values.
set(PICOBOOT_WIFI_INJECTION_KHZ 250000 CACHE STRING "CPU clock during the injection in kHz (design D27)")
set(PICOBOOT_WIFI_FLASH_CLKDIV 3 CACHE STRING "Flash clock divider, from clk_sys (design D27)")

# Test builds are 0.N.0-dev (N = the phase) plus the git hash; the first release is 1.0.0 (design D35)
set(PICOBOOT_WIFI_VERSION "0.2.0-dev")
execute_process(
    COMMAND git -c safe.directory=* rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE PICOBOOT_WIFI_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
)
if(NOT PICOBOOT_WIFI_GIT_HASH)
    set(PICOBOOT_WIFI_GIT_HASH "unknown")
endif()
set(PICOBOOT_WIFI_BUILD "${PICOBOOT_WIFI_VERSION}+${PICOBOOT_WIFI_GIT_HASH}")
message(STATUS "PicoBoot-WiFi ${PICOBOOT_WIFI_BUILD}")

# The flash layout lives in src/common/layout.h only; read its LAYOUT_* values for the linker checks
set(LAYOUT_H ${CMAKE_CURRENT_LIST_DIR}/src/common/layout.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LAYOUT_H})
file(STRINGS ${LAYOUT_H} LAYOUT_LINES REGEX "^#define LAYOUT_[A-Z0-9_]+ +0x[0-9A-Fa-f]+")
foreach(LINE IN LISTS LAYOUT_LINES)
    string(REGEX MATCH "^#define (LAYOUT_[A-Z0-9_]+) +(0x[0-9A-Fa-f]+)" UNUSED "${LINE}")
    set(${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
endforeach()
if(NOT LAYOUT_LOADER_SIZE OR NOT LAYOUT_SLOT_SIZE)
    message(FATAL_ERROR "Could not read the flash layout from ${LAYOUT_H}")
endif()

foreach(PART loader firmware)
    configure_file(linker/${PART}/memory_extra.incl.in ${CMAKE_BINARY_DIR}/linker/${PART}/memory_extra.incl @ONLY)
endforeach()

# Settings shared by the loader and the firmware
function(picoboot_wifi_target TARGET NAME DESCRIPTION)
    target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/src/common)
    target_compile_definitions(${TARGET} PRIVATE PICOBOOT_WIFI_BUILD="${PICOBOOT_WIFI_BUILD}")
    pico_set_program_name(${TARGET} ${NAME})
    pico_set_program_description(${TARGET} ${DESCRIPTION})
    pico_set_program_version(${TARGET} ${PICOBOOT_WIFI_BUILD})
    pico_set_program_url(${TARGET} "https://github.com/r3mko/PicoBoot-WiFi")
    pico_add_extra_outputs(${TARGET})
endfunction()

add_subdirectory(src/loader)
add_subdirectory(src/firmware)
