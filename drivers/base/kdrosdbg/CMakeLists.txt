
spec2def(kdrosdbg.dll kdrosdbg.spec)

list(APPEND SOURCE
    kdrosdbg.c
    kdrosdbg.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kdrosdbg.def)

add_library(kdrosdbg MODULE ${SOURCE})
set_module_type(kdrosdbg kerneldll ENTRYPOINT 0)
add_importlibs(kdrosdbg ntoskrnl hal)
target_link_libraries(kdrosdbg cportlib)
add_dependencies(kdrosdbg psdk)
add_cd_file(TARGET kdrosdbg DESTINATION reactos/system32 NO_CAB FOR all)
