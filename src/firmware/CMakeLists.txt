# The firmware (one per slot): injection, Wi-Fi and the web UI. It is linked at the start of
# flash; the loader maps slot A or B there with rom_chain_image() (design D7).

set(FIRMWARE_SOURCES
    main.c
    injection.c
    injection_pio.c
    payload.c
)

add_executable(firmware ${FIRMWARE_SOURCES})

# Extra warnings for our own sources only (the SDK sources are compiled into the same target)
set_source_files_properties(${FIRMWARE_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra"
)

pico_generate_pio_header(firmware ${CMAKE_CURRENT_LIST_DIR}/pio/injection.pio)

# Built-in IPL (gekkoboot r9.4), scrambled into ipl.h as in PicoBoot-DX; used when there is no
# valid payload
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(IPL_DOL ${CMAKE_SOURCE_DIR}/assets/ipl/gekkoboot_r9_4.dol)
set(IPL_H ${CMAKE_CURRENT_BINARY_DIR}/ipl.h)
add_custom_command(
    OUTPUT ${IPL_H}
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/process_ipl.py ${IPL_DOL} ${IPL_H}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/process_ipl.py ${IPL_DOL}
    COMMENT "Generating ipl.h from gekkoboot_r9_4.dol"
)
add_custom_target(firmware_ipl DEPENDS ${IPL_H})
add_dependencies(firmware firmware_ipl)

target_include_directories(firmware PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(firmware PRIVATE
    pico_stdlib
    hardware_pio
    hardware_dma
)

pico_add_linker_script_override_path(firmware ${CMAKE_BINARY_DIR}/linker/firmware FILES memory_extra.incl)

picoboot_wifi_target(firmware "picoboot-wifi" "PicoBoot-WiFi: GameCube IPL injection with Wi-Fi updates")
