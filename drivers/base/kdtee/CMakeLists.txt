
# Standard KDCOM already provides the correct import lib.
spec2def(kdtee.dll kdtee.spec)

list(APPEND SOURCE
    kdtee.c
    kdtee.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kdtee.def)

add_library(kdtee MODULE ${SOURCE})
set_module_type(kdtee kerneldll ENTRYPOINT 0)
#set_subsystem(kdtee native)
add_importlibs(kdtee ntoskrnl hal)
add_dependencies(kdtee psdk)
add_cd_file(TARGET kdtee DESTINATION reactos/system32 NO_CAB FOR all)
