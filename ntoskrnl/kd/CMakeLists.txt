
spec2def(kdterm.dll kdterm.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    kdio.c
    kdmain.c
    kdprompt.c
    kdps2kbd.c
    kdserial.c
    kdterminal.c
    kd.h)
    ## kdterm.c

if(ARCH STREQUAL "i386")
    list(APPEND SOURCE i386/kdserial.c)
elseif(ARCH STREQUAL "amd64")
    list(APPEND SOURCE i386/kdserial.c)
elseif(ARCH STREQUAL "arm")
    list(APPEND SOURCE arm/kdserial.c)
endif()

add_library(kdterm MODULE
    ${SOURCE}
    kdterm.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kdterm.def)

add_pch(kdterm kd.h SOURCE)

set_module_type(kdterm kerneldll ENTRYPOINT 0)
add_importlibs(kdterm ntoskrnl hal)
target_link_libraries(kdterm cportlib strtol)
add_dependencies(kdterm psdk bugcodes)
add_cd_file(TARGET kdterm DESTINATION reactos/system32 NO_CAB FOR all)
