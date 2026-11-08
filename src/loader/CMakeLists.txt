# The loader (design D7): a small image at the start of flash that picks a firmware slot and
# hands over to it with the boot ROM's rom_chain_image(). It must be quick, so it skips the
# SDK's clock setup and keeps the runtime minimal.

set(LOADER_SOURCES
    loader.c
    ${CMAKE_SOURCE_DIR}/src/common/records.c
    ${CMAKE_SOURCE_DIR}/src/common/bootdecide.c
)

add_executable(loader ${LOADER_SOURCES})

set_source_files_properties(${LOADER_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra"
)

target_link_libraries(loader PRIVATE
    pico_stdlib
    pico_bootrom
)

target_compile_definitions(loader PRIVATE
    # Stay on the clocks the boot ROM left running; the firmware sets up its own
    PICO_RUNTIME_SKIP_INIT_CLOCKS=1
)

pico_minimize_runtime(loader)

pico_add_linker_script_override_path(loader ${CMAKE_BINARY_DIR}/linker/loader FILES memory_extra.incl)

picoboot_wifi_target(loader "picoboot-wifi-loader" "PicoBoot-WiFi loader: picks a firmware slot")
