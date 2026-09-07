
list(APPEND SOURCE
    circularbuffer.c
    cleanup.c
    close.c
    create.c
    devctrl.c
    info.c
    legacy.c
    misc.c
    pnp.c
    power.c
    rw.c
    serial.c)

list(APPEND PCH_SKIP_SOURCE
    guid.c)

add_library(serial MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    serial.rc)
add_pch(serial serial.h "${PCH_SKIP_SOURCE}")

set_module_type(serial kernelmodedriver)
add_importlibs(serial ntoskrnl hal)
add_cd_file(TARGET serial DESTINATION reactos/system32/drivers FOR all)
