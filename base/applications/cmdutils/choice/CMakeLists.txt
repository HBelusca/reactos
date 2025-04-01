
include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/conutils)

add_executable(choice choice.c choice.rc)
set_module_type(choice win32cui UNICODE)
target_link_libraries(choice conutils ${PSEH_LIB})
add_importlibs(choice msvcrt kernel32)
add_cd_file(TARGET choice DESTINATION reactos/system32 FOR all)
