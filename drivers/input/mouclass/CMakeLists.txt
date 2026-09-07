
list(APPEND SOURCE
    misc.c
    mouclass.c)

list(APPEND PCH_SKIP_SOURCE
    guid.c)

add_library(mouclass MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    mouclass.rc)
add_pch(mouclass mouclass.h "${PCH_SKIP_SOURCE}")

set_module_type(mouclass kernelmodedriver)
target_link_libraries(mouclass ${PSEH_LIB})
add_importlibs(mouclass ntoskrnl hal)
add_cd_file(TARGET mouclass DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(mouclass_reg.inf)
