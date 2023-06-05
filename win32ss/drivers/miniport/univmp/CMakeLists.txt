
add_library(univmp MODULE univmp.c univmp.rc)
set_module_type(univmp kernelmodedriver)
add_importlibs(univmp ntoskrnl videoprt)

add_cd_file(TARGET univmp DESTINATION reactos/system32/drivers FOR all)
#add_registry_inf(univmp_reg.inf)
#add_driver_inf(univmp univmp.inf)
