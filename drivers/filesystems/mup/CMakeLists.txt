
list(APPEND SOURCE
     dfs.c
     mup.c
     dfs.h
     mup.h)

add_library(mup MODULE ${SOURCE} mup.rc)
add_pch(mup mup.h SOURCE)
set_module_type(mup kernelmodedriver)
target_link_libraries(mup ${PSEH_LIB})
add_importlibs(mup ntoskrnl hal)
add_cd_file(TARGET mup DESTINATION reactos/system32/drivers FOR all)
