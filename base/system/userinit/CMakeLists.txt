
list(APPEND SOURCE
    livecd.c
    userinit.c
    userinit.h)

add_executable(userinit ${SOURCE} userinit.rc)
add_pch(userinit userinit.h SOURCE)
target_link_libraries(userinit wine)
set_module_type(userinit win32gui UNICODE)
add_importlibs(userinit advapi32 userenv gdi32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET userinit DESTINATION reactos/system32 FOR all)
