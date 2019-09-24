
if((NOT ARCH STREQUAL "i386") AND (NOT ARCH STREQUAL "amd64"))
    message(FATAL_ERROR "Cannot build a StartROM for ARCH '" ${ARCH} "'.")
endif()

if(SEPARATE_DBG)
    # FIXME: http://sourceware.org/bugzilla/show_bug.cgi?id=11822
    set(CMAKE_LDR_PE_HELPER_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
    set(CMAKE_LDR_PE_HELPER_STANDARD_LIBRARIES_INIT "")
    set(CMAKE_LDR_PE_HELPER_STANDARD_LIBRARIES "-lgcc" CACHE STRING "Standard C Libraries")
endif()

if(MSVC)
    # We don't need hotpatching
    replace_compile_flags("/hotpatch" " ")

    # Explicitly use string pooling
    add_compile_flags("/GF")
endif()


## We would like to NOT use runtime checks here... But some libs we depend upon currently use them.
# remove_definitions(-D__RUNTIME_CHECKS__)
# replace_compile_flags("/RTC1" " ")


#
# The 16-bit stub code
#
if(ARCH STREQUAL "i386")
    CreateBootSectorTarget(frldr16
        ${CMAKE_CURRENT_SOURCE_DIR}/arch/realmode/i386.S
        ${CMAKE_CURRENT_BINARY_DIR}/frldr16.bin
        F800)
elseif(ARCH STREQUAL "amd64")
    CreateBootSectorTarget(frldr16
        ${CMAKE_CURRENT_SOURCE_DIR}/arch/realmode/amd64.S
        ${CMAKE_CURRENT_BINARY_DIR}/frldr16.bin
        F800)
endif()

#
# The 32-bit code
#
add_definitions(-D_NTHAL_ -D_BLDR_ -D_NTSYSTEM_)

list(APPEND STARTROM_ASM_SOURCE)

if(ARCH STREQUAL "i386")
    list(APPEND STARTROM_ASM_SOURCE
        arch/i386/entry.S
        # arch/i386/drvmap.S
        arch/i386/int386.S
        # arch/i386/pnpbios.S
        arch/i386/i386trap.S
        # arch/i386/linux.S
        )

elseif(ARCH STREQUAL "amd64")
    # list(APPEND STARTROM_ASM_SOURCE
        # arch/amd64/entry.S
        # arch/amd64/int386.S
        # arch/amd64/pnpbios.S)

endif()

add_asm_files(startrom_asm ${STARTROM_ASM_SOURCE})
list(APPEND STARTROM_SOURCE
    ${startrom_asm}
    main.c
    func.c)

add_executable(startrom_pe ${STARTROM_SOURCE})

target_link_libraries(startrom_pe libcntpr)

if(NOT MSVC AND SEPARATE_DBG)
    set_target_properties(startrom_pe PROPERTIES LINKER_LANGUAGE LDR_PE_HELPER)
endif()

if(MSVC)
    add_target_link_flags(startrom_pe "/ignore:4078 /ignore:4254 /DRIVER /FILEALIGN:0x200 /ALIGN:0x200") # /FIXED
    add_linker_script(startrom_pe freeldr_i386.msvc.lds)
else()
    add_target_link_flags(startrom_pe "-Wl,--strip-all,--exclude-all-symbols,--file-alignment,0x200,--section-alignment,0x200")
    # add_linker_script(startrom_pe freeldr_i386.lds)
endif()

## We would like to NOT use runtime checks here... But some libs we depend upon currently use them.
if(STACK_PROTECTOR)
    target_link_libraries(startrom_pe gcc_ssp)
elseif(RUNTIME_CHECKS)
    target_link_libraries(startrom_pe runtmchk)
endif()

add_dependencies(startrom_pe asm)

set_image_base(startrom_pe 0x10000)
set_subsystem(startrom_pe native)
set_entrypoint(startrom_pe RealEntryPoint)

add_custom_command(
    # TARGET startrom_pe POST_BUILD
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/startrom_pe_flat.exe
    COMMAND native-obj2bin $<TARGET_FILE:startrom_pe> ${CMAKE_CURRENT_BINARY_DIR}/startrom_pe_flat.exe 0x10000
    DEPENDS native-obj2bin
    VERBATIM)

concatenate_files(
    ${CMAKE_CURRENT_BINARY_DIR}/startrom.com
    ${CMAKE_CURRENT_BINARY_DIR}/frldr16.bin # $<SHELL_PATH:$<TARGET_FILE:frldr16>>
    ${CMAKE_CURRENT_BINARY_DIR}/startrom_pe_flat.exe) # ${CMAKE_CURRENT_BINARY_DIR}/$<TARGET_FILE_NAME:startrom_pe> # $<SHELL_PATH:$<TARGET_FILE:startrom_pe>>
add_custom_target(startrom ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/startrom.com ${CMAKE_CURRENT_BINARY_DIR}/frldr16.bin ${CMAKE_CURRENT_BINARY_DIR}/startrom_pe_flat.exe)
