PROJECT(ramdiskctl)

include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers)

add_executable(ramdiskctl ramdiskctl.c)
set_module_type(ramdiskctl win32cui UNICODE)
##target_link_libraries(ramdiskctl vhdlib)
add_importlibs(ramdiskctl rpcrt4 msvcrt kernel32 ntdll)
add_cd_file(TARGET ramdiskctl DESTINATION reactos/system32 FOR all)
