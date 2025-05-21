
include_directories(
    ${REACTOS_SOURCE_DIR}/win32ss/user/winsrv/concfg)

spec2def(console.dll console.spec)

list(APPEND SOURCE
    console.c
    options.c
    font.c
    layout.c
    colors.c
    utils.c
    console.h)

add_rc_deps(console.rc ${CMAKE_CURRENT_SOURCE_DIR}/res/terminal.ico)

add_library(console MODULE
    ${SOURCE}
    console.rc
    ${CMAKE_CURRENT_BINARY_DIR}/console.def)
add_pch(console console.h SOURCE)

set_module_type(console cpl UNICODE)
set_target_properties(console PROPERTIES SUFFIX ".dll")

add_dependencies(console psdk)
target_link_libraries(console concfg uuid)
add_delay_importlibs(console shell32)
add_importlibs(console advapi32 comctl32 gdi32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET console DESTINATION reactos/system32 FOR all)
