list(APPEND SOURCE
    bochsmp.c
    bochsmp.h)

add_library(bochsmp MODULE ${SOURCE} bochsmp.rc)
add_pch(bochsmp bochsmp.h SOURCE)
set_module_type(bochsmp kernelmodedriver)
add_importlibs(bochsmp videoprt)
target_link_libraries(bochsmp libcntpr)
add_cd_file(TARGET bochsmp DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(bochsmp_reg.inf)
add_driver_inf(bochsmp bochsmp.inf)
