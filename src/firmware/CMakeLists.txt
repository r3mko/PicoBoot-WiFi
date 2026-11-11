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
    storage.c
    settings.c
    json.c
    wifi.c
    api.c
    update.c
    updater.c
    slots.c
    bininfo.c
    sha.c
    payloadup.c
    payloads.c
    ${CMAKE_SOURCE_DIR}/src/common/records.c
    ${CMAKE_SOURCE_DIR}/src/common/ringlog.c
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

# The web UI (src/web/), gzipped into lwIP httpd's built-in file table (fsdata_custom.c, which
# lwIP's fs.c includes)
set(WEB_DIR ${CMAKE_SOURCE_DIR}/src/web)
set(FSDATA_C ${CMAKE_CURRENT_BINARY_DIR}/fsdata_custom.c)
file(GLOB WEB_FILES CONFIGURE_DEPENDS ${WEB_DIR}/*)
add_custom_command(
    OUTPUT ${FSDATA_C}
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/mkfsdata.py ${WEB_DIR} ${FSDATA_C}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/mkfsdata.py ${WEB_FILES}
    COMMENT "Generating fsdata_custom.c from src/web"
)
add_custom_target(firmware_fsdata DEPENDS ${FSDATA_C})
add_dependencies(firmware firmware_fsdata)

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
    pico_lwip_http
    pico_sha256
)

# The CYW43 blob isn't compiled in: the driver reads it from Wi-Fi area A or B (design D14),
# through the same include-file hook as the SDK's partition variant
target_compile_definitions(firmware PRIVATE
    CYW43_CHIPSET_FIRMWARE_INCLUDE_FILE="cyw43_fixed_firmware.h"
    # A failed allocation returns NULL, which net.c and lwIP answer with 503 or a dropped packet; the
    # SDK's default panics, a crash that D12 counts (phase 5 security review)
    PICO_MALLOC_PANIC=0
)

# The access point is at 192.168.4.1
pico_configure_ip4_address(firmware PRIVATE
    CYW43_DEFAULT_IP_AP_ADDRESS 192.168.4.1
)

# The Wi-Fi area image: the SDK's Wi-Fi-only CYW43439 blob behind our header (tools/mkwifiblob.py)
# PICOBOOT_WIFI_BLOB=marked: the same blob with a marker in its spare header bytes, a blob that works
# but differs, the stand-in for an update that brings a new blob (plan step 3.8)
set(WIFI_BLOB_H ${PICO_CYW43_DRIVER_PATH}/firmware/w43439A0_7_95_49_00_combined.h)
set(WIFI_BLOB_MARKER_ARGS "")
if(PICOBOOT_WIFI_BLOB STREQUAL "marked")
    set(WIFI_BLOB_MARKER_ARGS --marker newblob)
endif()
set(WIFI_BLOB_BIN ${CMAKE_BINARY_DIR}/wifi-blob.bin)
add_custom_command(
    OUTPUT ${WIFI_BLOB_BIN}
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/mkwifiblob.py ${WIFI_BLOB_H} ${WIFI_BLOB_BIN}
            ${WIFI_BLOB_MARKER_ARGS}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/mkwifiblob.py ${WIFI_BLOB_H} ${CMAKE_SOURCE_DIR}/src/common/layout.h
    COMMENT "Building the Wi-Fi area image"
)
add_custom_target(wifi_blob ALL DEPENDS ${WIFI_BLOB_BIN})

# Fast flash reads (quad I/O, continuous read) set up by the firmware itself. A chained image is
# left in the boot ROM's slowest mode (serial 03h at clk_sys/256: 0.06 MB/s on the bench, far too
# slow for the payload DMA), and the SDK restores whatever XIP setup is in boot RAM after each flash
# write. With PICO_EMBED_XIP_SETUP, crt0 runs this boot2 at start-up and copies it into boot RAM.
# Divider PICOBOOT_WIFI_FLASH_CLKDIV, 3 by default: 83 MHz flash clock during the injection's
# 250 MHz, 50 MHz at 150 MHz (design D27).
pico_define_boot_stage2(firmware_xip_setup ${PICO_SDK_PATH}/src/rp2350/boot_stage2/boot2_w25q080.S)
target_compile_definitions(firmware_xip_setup PRIVATE PICO_FLASH_SPI_CLKDIV=${PICOBOOT_WIFI_FLASH_CLKDIV})
pico_set_boot_stage2(firmware firmware_xip_setup)
target_compile_definitions(firmware PRIVATE PICO_EMBED_XIP_SETUP=1)
target_compile_definitions(firmware PRIVATE PICOBOOT_WIFI_INJECTION_KHZ=${PICOBOOT_WIFI_INJECTION_KHZ})
target_compile_definitions(firmware PRIVATE PICOBOOT_WIFI_BOARD="${PICO_BOARD}")   # Update files name it (D11)
target_compile_definitions(firmware PRIVATE PICOBOOT_WIFI_TEST_CRASH=${PICOBOOT_WIFI_TEST_CRASH_CODE})


if(PICOBOOT_WIFI_USB_TRACE)
    pico_enable_stdio_usb(firmware 1)
    target_compile_definitions(firmware PRIVATE
        PICOBOOT_WIFI_USB_TRACE=1
        PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=10000
    )
endif()

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

# The over-the-air update file (design D11): header, slot image and Wi-Fi blob (tools/mkupdate.py)
set(UPDATE_BIN ${CMAKE_BINARY_DIR}/picoboot-wifi-update.bin)
add_custom_command(
    OUTPUT ${UPDATE_BIN}
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/mkupdate.py
        --firmware $<TARGET_FILE_DIR:firmware>/firmware.bin
        --blob ${WIFI_BLOB_BIN}
        --version ${PICOBOOT_WIFI_BUILD}
        --board ${PICO_BOARD}
        --output ${UPDATE_BIN}
    DEPENDS firmware ${WIFI_BLOB_BIN} ${CMAKE_SOURCE_DIR}/tools/mkupdate.py
        ${CMAKE_SOURCE_DIR}/src/common/layout.h ${CMAKE_SOURCE_DIR}/src/common/bootinfo.h
    COMMENT "Building picoboot-wifi-update.bin"
)
add_custom_target(update_bin ALL DEPENDS ${UPDATE_BIN})
