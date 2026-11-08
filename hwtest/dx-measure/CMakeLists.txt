# PicoBoot-WiFi phase 0 measurement build (implementation plan step 0.1):
# PicoBoot-DX v0.10 for the Pico 2 W, plus the passive CS counter (design D31) and an
# access point with a report page (design D32). See README.md.

cmake_minimum_required(VERSION 3.13...3.31)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK 2 (must be before project)
include(pico_sdk_2_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.3.0")
    message(FATAL_ERROR "Needs Pico SDK 2.3.0 or newer (found ${PICO_SDK_VERSION_STRING})")
endif()

project(dx-measure
    LANGUAGES C CXX ASM
)

pico_sdk_init()

if(NOT PICO_BOARD STREQUAL "pico2_w")
    message(FATAL_ERROR "The phase 0 measurement build is for the Pico 2 W only (PICO_BOARD=pico2_w)")
endif()

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

# Build ID shown on the report page: short git hash, if available
execute_process(
    COMMAND git -c safe.directory=* rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
)
if(NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()

# Built-in IPL (gekkoboot r9.4), scrambled into ipl.h as in PicoBoot-DX
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(IPL_DOL ${REPO_ROOT}/assets/ipl/gekkoboot_r9_4.dol)
set(IPL_H ${CMAKE_CURRENT_BINARY_DIR}/ipl.h)
add_custom_command(
    OUTPUT ${IPL_H}
    COMMAND ${Python3_EXECUTABLE} -I ${REPO_ROOT}/tools/process_ipl.py ${IPL_DOL} ${IPL_H}
    DEPENDS ${REPO_ROOT}/tools/process_ipl.py ${IPL_DOL}
    COMMENT "Generating ipl.h from gekkoboot_r9_4.dol"
)
add_custom_target(dx-measure-ipl DEPENDS ${IPL_H})

add_executable(dx-measure
    src/picoboot-dx.c
    src/pio.c
    src/measure.c
    src/bootlog.c
    src/report.c
    src/net.c
    src/dhcpserver/dhcpserver.c
    src/dnsserver/dnsserver.c
)
add_dependencies(dx-measure dx-measure-ipl)

# Extra warnings for our own sources only
set_source_files_properties(src/picoboot-dx.c src/pio.c src/measure.c src/bootlog.c src/report.c src/net.c PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra"
)

pico_generate_pio_header(dx-measure ${CMAKE_CURRENT_LIST_DIR}/src/picoboot-dx.pio)
pico_generate_pio_header(dx-measure ${CMAKE_CURRENT_LIST_DIR}/src/measure.pio)

pico_set_program_name(dx-measure "picoboot-wifi-dx-measure")
pico_set_program_description(dx-measure "PicoBoot-WiFi phase 0 measurement build (PicoBoot-DX v0.10 + boot timing + Wi-Fi report)")
pico_set_program_version(dx-measure "0.0.0-dev+${GIT_HASH}")
pico_set_program_url(dx-measure "https://github.com/r3mko/PicoBoot-WiFi")

target_compile_definitions(dx-measure PRIVATE
    PHASE0_BUILD_ID="${GIT_HASH}"
)

target_include_directories(dx-measure PRIVATE
    src
    src/dhcpserver
    src/dnsserver
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(dx-measure PRIVATE
    pico_stdlib
    pico_unique_id
    hardware_pio
    hardware_dma
    hardware_flash
    hardware_adc
    pico_cyw43_arch_lwip_poll
)

# The access point is at 192.168.4.1
pico_configure_ip4_address(dx-measure PRIVATE
    CYW43_DEFAULT_IP_AP_ADDRESS 192.168.4.1
)

# Payload region and symbols, as in PicoBoot-DX
pico_add_linker_script_override_path(dx-measure ${CMAKE_CURRENT_LIST_DIR}/linker FILES memory_extra.incl)

pico_add_extra_outputs(dx-measure)
