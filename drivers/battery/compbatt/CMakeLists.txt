
list(APPEND SOURCE
    compbatt.c
    compmisc.c
    comppnp.c)

list(APPEND PCH_SKIP_SOURCE
    guid.c)

add_library(compbatt MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    compbatt.rc)
add_pch(compbatt compbatt.h "${PCH_SKIP_SOURCE}")

set_module_type(compbatt kernelmodedriver)
add_importlibs(compbatt ntoskrnl hal battc)
add_cd_file(TARGET compbatt DESTINATION reactos/system32/drivers FOR all)
