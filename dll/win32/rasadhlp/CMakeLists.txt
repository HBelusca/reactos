
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/winsock)
spec2def(rasadhlp.dll rasadhlp.spec)

list(APPEND SOURCE
    autodial.c
    init.c
    winsock.c
    precomp.h
    ${CMAKE_CURRENT_BINARY_DIR}/rasadhlp_stubs.c)

add_library(rasadhlp MODULE
    ${SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/rasadhlp.def)
add_pch(rasadhlp precomp.h SOURCE)

set_module_type(rasadhlp win32dll)
add_importlibs(rasadhlp ws2_32 msvcrt kernel32 ntdll)
add_cd_file(TARGET rasadhlp DESTINATION reactos/system32 FOR all)
