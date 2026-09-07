
add_definitions(-D__USE_W32_SOCKETS)

list(APPEND SOURCE
    finger.c
    err.c
    getopt.c
    net.c
    precomp.h)

add_executable(finger ${SOURCE} finger.rc)
add_pch(finger precomp.h SOURCE)
set_module_type(finger win32cui)
add_importlibs(finger ws2_32 msvcrt kernel32)
add_cd_file(TARGET finger DESTINATION reactos/system32 FOR all)
