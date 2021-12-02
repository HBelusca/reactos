
PROJECT(NTVDM)

# Define to control the inclusion of DOS emulation inside NTVDM.
set(USE_DOS TRUE)

include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/fast486)

set(NTVDM_SPEC ntvdm.spec)
if(USE_DOS)
    add_definitions(-D_USE_DOS_)
    include(dos.cmake)
    set(DOS_LIB dos)
    list(APPEND NTVDM_SPEC dos.spec)
endif()

spec2def(ntvdm.exe "${NTVDM_SPEC}" ADD_IMPORTLIB)

list(APPEND SOURCE
    bios/bios32/bios32.c
    bios/bios32/dskbios32.c
    bios/bios32/kbdbios32.c
    bios/bios32/moubios32.c
    bios/bios32/vbe.c
    bios/bios32/vidbios32.c
    bios/bios.c
    bios/kbdbios.c
    bios/rom.c
    bios/umamgr.c
    bios/vidbios.c
    cpu/bop.c
    cpu/callback.c
    cpu/cpu.c
    cpu/registers.c
    hardware/cmos.c
    hardware/disk.c
    hardware/dma.c
    hardware/keyboard.c
    hardware/mouse.c
    hardware/pic.c
    hardware/pit.c
    hardware/ppi.c
    hardware/ps2.c
    hardware/sound/speaker.c
    hardware/video/svga.c
    clock.c
    emulator.c
    int32.c
    io.c
    memory.c
    utils.c
    vddsup.c
    ntvdm.c
    ntvdm.h)

file(GLOB ntvdm_rc_deps res/*.*)
add_rc_deps(ntvdm.rc ${ntvdm_rc_deps})

add_executable(ntvdm
    ${SOURCE}
    ntvdm.rc
    ${CMAKE_CURRENT_BINARY_DIR}/ntvdm.def)

set_target_properties(ntvdm
    PROPERTIES
    ENABLE_EXPORTS TRUE
    DEFINE_SYMBOL "")

add_pch(ntvdm ntvdm.h SOURCE)

set_module_type(ntvdm win32cui UNICODE IMAGEBASE 0x0F000000)
target_link_libraries(ntvdm fast486 ${DOS_LIB} ${PSEH_LIB})
add_importlibs(ntvdm user32 gdi32 advapi32 comdlg32 msvcrt kernel32 ntdll)
add_cd_file(TARGET ntvdm DESTINATION reactos/system32 FOR all)
