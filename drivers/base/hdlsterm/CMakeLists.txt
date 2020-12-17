
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers)

list(APPEND SOURCE
    hdlsterm.c
    hdlsterm.h)

add_library(hdlsterm MODULE ${SOURCE} hdlsterm.rc)
set_module_type(hdlsterm kernelmodedriver)
target_link_libraries(hdlsterm ${PSEH_LIB})
add_importlibs(hdlsterm ntoskrnl hal)
add_pch(hdlsterm hdlsterm.h SOURCE)
add_cd_file(TARGET hdlsterm DESTINATION reactos/system32/drivers NO_CAB FOR all)
