
list(APPEND SOURCE
    fdo.c
    misc.c
    pdo.c
    parport.c
    parport.h)

add_library(parport MODULE ${SOURCE} parport.rc)
add_pch(parport parport.h SOURCE)

set_module_type(parport kernelmodedriver)
add_importlibs(parport ntoskrnl hal)
add_cd_file(TARGET parport DESTINATION reactos/system32/drivers FOR all)
