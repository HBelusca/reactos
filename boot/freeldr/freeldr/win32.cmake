##
## PROJECT:     FreeLoader
## LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
## PURPOSE:     Build definitions for Win32 testing
## COPYRIGHT:   Copyright 2024 Hermès Bélusca-Maïto <hermes.belusca-maito@reactos.org>
##

include_directories(BEFORE
    ${REACTOS_SOURCE_DIR}/boot/freeldr/freeldr
    ${REACTOS_SOURCE_DIR}/boot/freeldr/freeldr/include
    ${REACTOS_SOURCE_DIR}/boot/freeldr/freeldr/include/arch ## /win32
    )

##
## Remove stuff that we don't support yet!!
##
list(REMOVE_ITEM FREELDR_BOOTLIB_SOURCE
    lib/cache/blocklist.c
    lib/cache/cache.c
    lib/comm/rs232.c
    lib/mm/meminit.c ## FIXME
    lib/mm/mm.c      ## FIXME
    lib/mm/heap.c
    )
##
##
list(APPEND FREELDR_BOOTLIB_SOURCE
    lib/fs/win32fs.c
    lib/mm/win32mm.c
    lib/mm/win32heap.c)


##
## Remove stuff that we don't support yet!!
##
if(ARCH STREQUAL "i386")
    list(REMOVE_ITEM FREELDR_ARC_SOURCE
        arch/i386/halstub.c
        arch/i386/ntoskrnl.c
        disk/scsiport.c)

elseif(ARCH STREQUAL "amd64")
    list(REMOVE_ITEM FREELDR_ARC_SOURCE
        arch/i386/ntoskrnl.c)

elseif(ARCH STREQUAL "arm")
else()
    #TBD
endif()
##
##

list(APPEND WIN32LDR_ARC_SOURCE
    ${FREELDR_ARC_SOURCE}
    arch/win32/machwin32.c
    arch/win32/stubs.c
    arch/win32/win32beep.c
    arch/win32/win32cons.c
    arch/win32/win32disk.c
    arch/win32/win32hw.c
    ## arch/win32/win32mem.c
    arch/win32/win32ldr.c
    arch/win32/win32rtc.c
    arch/win32/win32tuivid.c
    arch/win32/win32video.c
    ## arch/vgafont.c
    )

if(ARCH STREQUAL "i386")
    ## list(APPEND WIN32LDR_ARC_SOURCE
    ##     arch/i386/i386idt.c)
    ## list(APPEND WIN32LDR_COMMON_ASM_SOURCE
    ##     arch/uefi/i386/uefiasm.S
    ##     arch/i386/i386trap.S)
    list(APPEND WIN32LDR_ARC_SOURCE
        arch/i386/drivemap.c)
elseif(ARCH STREQUAL "amd64")
    ## list(APPEND WIN32LDR_COMMON_ASM_SOURCE
    ##     arch/uefi/amd64/uefiasm.S)
    list(APPEND WIN32LDR_ARC_SOURCE
        arch/i386/drivemap.c)
elseif(ARCH STREQUAL "arm")
    list(APPEND WIN32LDR_ARC_SOURCE
        arch/arm/macharm.c
        arch/arm/debug.c)
    #TBD
elseif(ARCH STREQUAL "arm64")
    #TBD
else()
    #TBD
endif()

## add_asm_files(win32freeldr_common_asm ${FREELDR_COMMON_ASM_SOURCE} ${WIN32LDR_COMMON_ASM_SOURCE})

add_library(win32freeldr_common
    ## ${win32freeldr_common_asm}
    ${WIN32LDR_ARC_SOURCE}
    ${FREELDR_BOOTLIB_SOURCE}
    ${FREELDR_BOOTMGR_SOURCE}
    ## ${FREELDR_NTLDR_SOURCE}
    )

target_compile_definitions(win32freeldr_common PRIVATE MY_WIN32) ##FIXME

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Prevent using SSE (no support in freeldr)
    target_compile_options(win32freeldr_common PUBLIC -mno-sse)
endif()

set(PCH_SOURCE
    ${WIN32LDR_ARC_SOURCE}
    ${FREELDR_BOOTLIB_SOURCE}
    ${FREELDR_BOOTMGR_SOURCE}
    ## ${FREELDR_NTLDR_SOURCE}
    )

add_pch(win32freeldr_common include/arch/win32ldr.h PCH_SOURCE)
add_dependencies(win32freeldr_common bugcodes asm xdk)

## ## GCC builds need this extra thing for some reason...
## if(ARCH STREQUAL "i386" AND NOT MSVC)
##     target_link_libraries(win32freeldr_common mini_hal)
## endif()


spec2def(win32ldr.exe freeldr.spec)

##
## Remove stuff that we don't support yet!!
##
list(REMOVE_ITEM FREELDR_BASE_SOURCE
    ## freeldr.c
    ntldr/setupldr.c
    ntldr/inffile.c)
##
##

list(APPEND WIN32LDR_BASE_SOURCE
    include/arch/win32ldr.h
    arch/win32/win32ldr.c
    ${FREELDR_BASE_SOURCE})

if(ARCH STREQUAL "i386")
    # Must be included together with disk/scsiport.c
    list(APPEND WIN32LDR_BASE_SOURCE
        ${CMAKE_CURRENT_BINARY_DIR}/win32ldr.def)
endif()

add_executable(win32ldr ${WIN32LDR_BASE_SOURCE})

target_compile_definitions(win32ldr PRIVATE MY_WIN32) ##FIXME

# On AMD64 we only map 1GB with freeloader, tell UEFI to keep us low!
if(ARCH STREQUAL "amd64")
    set_image_base(win32ldr 0x10000)
endif()

if(MSVC)
## if(NOT ARCH STREQUAL "arm")
##     target_link_options(win32ldr PRIVATE /DYNAMICBASE:NO)
## endif()
    ## target_link_options(win32ldr PRIVATE /NXCOMPAT:NO /ignore:4078 /ignore:4254 /DRIVER)
    # We don't need hotpatching
    remove_target_compile_option(win32ldr "/hotpatch")
else()
    target_link_options(win32ldr PRIVATE -Wl,--exclude-all-symbols,--file-alignment,0x200,--section-alignment,0x200)
endif()

set_image_base(win32ldr 0x10000)
set_subsystem(win32ldr console) ## win32cui win32gui (windows)
set_entrypoint(win32ldr main) ## mainCRTStartup WinMainCRTStartup

if(ARCH STREQUAL "i386")
    target_link_libraries(win32ldr mini_hal)
endif()

target_link_libraries(win32ldr win32freeldr_common cportlib blcmlib blrtl libcntpr)

# dynamic analysis switches
if(STACK_PROTECTOR)
    target_sources(win32ldr PRIVATE $<TARGET_OBJECTS:gcc_ssp_nt>)
endif()

if(RUNTIME_CHECKS)
    target_link_libraries(win32ldr runtmchk)
endif()

add_importlibs(win32ldr kernel32 ntdll)

add_dependencies(win32ldr xdk)
