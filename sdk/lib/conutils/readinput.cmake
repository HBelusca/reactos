
list(APPEND SOURCE
    readinput.c
    # readinput.h
    )

add_library(conutils_readinput ${SOURCE})
# add_pch(conutils_readinput readinput.h SOURCE)
## add_dependencies(conutils_readinput xdk)
# target_link_libraries(conutils_readinput conutils ${PSEH_LIB})
add_importlibs(conutils_readinput msvcrt kernel32 user32)
