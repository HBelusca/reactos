
list(APPEND SOURCE
    cmbatt.c
    cmexec.c
    cmbpnp.c
    cmbwmi.c)

list(APPEND PCH_SKIP_SOURCE
    guid.c)

add_library(cmbatt MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    cmbatt.rc)
add_pch(cmbatt cmbatt.h "${PCH_SKIP_SOURCE}")

set_module_type(cmbatt kernelmodedriver)
add_importlibs(cmbatt ntoskrnl hal battc wmilib)
add_cd_file(TARGET cmbatt DESTINATION reactos/system32/drivers FOR all)
