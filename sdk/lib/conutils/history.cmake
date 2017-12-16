
list(APPEND SOURCE
    history.c
    # history.h
    )

add_library(conutils_history ${SOURCE})
# add_pch(conutils_history history.h SOURCE)
## add_dependencies(conutils_history xdk)
add_importlibs(conutils_history msvcrt kernel32)
