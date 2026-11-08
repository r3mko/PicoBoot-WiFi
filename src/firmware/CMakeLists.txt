# The firmware (one per slot): injection, Wi-Fi and the web UI. It is linked at the start of
# flash; the loader maps slot A or B there with rom_chain_image() (design D7).

set(FIRMWARE_SOURCES
    main.c
    injection.c
    injection_pio.c
    payload.c
    counters.c
    wifiblob.c
    net.c
    page.c
    ${CMAKE_SOURCE_DIR}/src/common/records.c
)

add_executable(firmware
    ${FIRMWARE_SOURCES}
    net/dhcpserver/dhcpserver.c
    net/dnsserver/dnsserver.c
)

# Extra warnings for our own sources only (the SDK sources are compiled into the same target)
set_source_files_properties(${FIRMWARE_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra"
)

pico_generate_pio_header(firmware ${CMAKE_CURRENT_LIST_DIR}/pio/injection.pio)
pico_generate_pio_header(firmware ${CMAKE_CURRENT_LIST_DIR}/pio/counters.pio)

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
    ${CMAKE_CURRENT_LIST_DIR}/net
    ${CMAKE_CURRENT_LIST_DIR}/net/dhcpserver
    ${CMAKE_CURRENT_LIST_DIR}/net/dnsserver
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(firmware PRIVATE
    pico_stdlib
    pico_unique_id
    pico_bootrom
    boot_picoboot_headers
    hardware_pio
    hardware_dma
    hardware_adc
    pico_cyw43_arch_lwip_poll
)

# The CYW43 blob isn't compiled in: the driver reads it from Wi-Fi area A or B (design D14),
# through the same include-file hook as the SDK's partition variant
target_compile_definitions(firmware PRIVATE
    CYW43_CHIPSET_FIRMWARE_INCLUDE_FILE="cyw43_fixed_firmware.h"
)

# The access point is at 192.168.4.1
pico_configure_ip4_address(firmware PRIVATE
    CYW43_DEFAULT_IP_AP_ADDRESS 192.168.4.1
)

# The Wi-Fi area image: the SDK's Wi-Fi-only CYW43439 blob behind our header (tools/mkwifiblob.py)
set(WIFI_BLOB_H ${PICO_CYW43_DRIVER_PATH}/firmware/w43439A0_7_95_49_00_combined.h)
set(WIFI_BLOB_BIN ${CMAKE_BINARY_DIR}/wifi-blob.bin)
add_custom_command(
    OUTPUT ${WIFI_BLOB_BIN}
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/mkwifiblob.py ${WIFI_BLOB_H} ${WIFI_BLOB_BIN}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/mkwifiblob.py ${WIFI_BLOB_H} ${CMAKE_SOURCE_DIR}/src/common/layout.h
    COMMENT "Building the Wi-Fi area image"
)
add_custom_target(wifi_blob ALL DEPENDS ${WIFI_BLOB_BIN})

pico_add_linker_script_override_path(firmware ${CMAKE_BINARY_DIR}/linker/firmware FILES memory_extra.incl)

picoboot_wifi_target(firmware "picoboot-wifi" "PicoBoot-WiFi: GameCube IPL injection with Wi-Fi updates")

# The USB install UF2 (design D24): loader, blank boot control and settings, Wi-Fi A and slot A
set(INSTALL_UF2 ${CMAKE_BINARY_DIR}/picoboot-wifi-install.uf2)
add_custom_command(
    OUTPUT ${INSTALL_UF2}
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/mkuf2.py
        --loader $<TARGET_FILE_DIR:loader>/loader.bin
        --firmware $<TARGET_FILE_DIR:firmware>/firmware.bin
        --wifi ${WIFI_BLOB_BIN}
        --out ${INSTALL_UF2}
    DEPENDS loader firmware ${WIFI_BLOB_BIN} ${CMAKE_SOURCE_DIR}/tools/mkuf2.py ${CMAKE_SOURCE_DIR}/src/common/layout.h
    COMMENT "Building picoboot-wifi-install.uf2"
)
add_custom_target(install_uf2 ALL DEPENDS ${INSTALL_UF2})
