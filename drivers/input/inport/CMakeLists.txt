
list(APPEND SOURCE
    hardware.c
    inport.c
    inport.h
    ioctl.c
    pnp.c
    wmi.c)

add_library(inport MODULE ${SOURCE} inport.rc)
add_pch(inport inport.h SOURCE)

set_module_type(inport kernelmodedriver)
add_importlibs(inport ntoskrnl hal wmilib)
add_cd_file(TARGET inport DESTINATION reactos/system32/drivers FOR all)
