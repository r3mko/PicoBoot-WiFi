# The loader stub for ordinary .dol files (design D43): PowerPC, assembled with Ubuntu's
# binutils-powerpc-linux-gnu (docker/Dockerfile), for the 603 family the GameCube's 750 belongs to
# (-m603: binutils' -m750cl lacks the 750's mftb). The converters carry its bytes,
# src/dolstub/dolstub.bin; with PICOBOOT_BUILD_DIR, tests/test_dolstub.py checks that this build
# gives the same bytes.

find_program(PPC_AS powerpc-linux-gnu-as)
find_program(PPC_LD powerpc-linux-gnu-ld)
find_program(PPC_OBJCOPY powerpc-linux-gnu-objcopy)

if(PPC_AS AND PPC_LD AND PPC_OBJCOPY)
    set(DOLSTUB_BIN ${CMAKE_CURRENT_BINARY_DIR}/dolstub.bin)
    add_custom_command(
        OUTPUT ${DOLSTUB_BIN}
        COMMAND ${PPC_AS} -mregnames -m603 -o dolstub.o ${CMAKE_CURRENT_LIST_DIR}/dolstub.S
        COMMAND ${PPC_LD} -T ${CMAKE_CURRENT_LIST_DIR}/link.ld -o dolstub.elf dolstub.o
        COMMAND ${PPC_OBJCOPY} -O binary dolstub.elf ${DOLSTUB_BIN}
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/dolstub.S ${CMAKE_CURRENT_LIST_DIR}/link.ld
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Assembling the D43 loader stub"
    )
    add_custom_target(dolstub ALL DEPENDS ${DOLSTUB_BIN})
else()
    message(WARNING "No PowerPC binutils (binutils-powerpc-linux-gnu): the D43 loader stub isn't built")
endif()
