# The firmware (one per slot): injection, Wi-Fi and the web UI. It is linked at the start of
# flash; the loader maps slot A or B there with rom_chain_image() (design D7).

add_executable(firmware
    main.c
)

set_source_files_properties(main.c PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra"
)

target_link_libraries(firmware PRIVATE
    pico_stdlib
)

pico_add_linker_script_override_path(firmware ${CMAKE_BINARY_DIR}/linker/firmware FILES memory_extra.incl)

picoboot_wifi_target(firmware "picoboot-wifi" "PicoBoot-WiFi: GameCube IPL injection with Wi-Fi updates")
