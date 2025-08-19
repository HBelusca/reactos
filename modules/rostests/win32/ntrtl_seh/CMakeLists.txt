
add_executable(ntrtl_seh ntrtl_seh.c)
target_link_libraries(ntrtl_seh ${PSEH_LIB})
set_module_type(ntrtl_seh win32gui)
add_importlibs(ntrtl_seh user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET ntrtl_seh DESTINATION reactos/system32 FOR all)
## add_rostests_file(TARGET ntrtl_seh SUBDIR suppl)
