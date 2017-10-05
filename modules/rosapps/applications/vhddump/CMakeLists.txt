PROJECT(vhddump)

include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/vhdlib)

add_executable(vhddump vhddump.c virtdisk_driver.c vhdsupp.c utils.c)
set_module_type(vhddump win32cui UNICODE)
target_link_libraries(vhddump vhdlib)
add_importlibs(vhddump rpcrt4 msvcrt kernel32 ntdll)
add_cd_file(TARGET vhddump DESTINATION reactos/system32 FOR all)
