
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers)

list(APPEND SOURCE
    font.c
    ntcondd.c
    ntcondd.h)

add_library(ntcondd MODULE ${SOURCE} ntcondd.rc)
set_module_type(ntcondd kernelmodedriver)
add_importlibs(ntcondd ntoskrnl hal)
add_pch(ntcondd ntcondd.h SOURCE)
add_cd_file(TARGET ntcondd DESTINATION reactos/system32/drivers NO_CAB FOR all)
