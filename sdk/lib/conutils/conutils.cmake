
list(APPEND SOURCE
    stream.c
    instream.c
    outstream.c
    pager.c
    screen.c
    utils.c
    # conutils.h
    )

add_library(conutils ${SOURCE})
# add_pch(conutils conutils.h SOURCE)
add_dependencies(conutils xdk)
target_link_libraries(conutils ${PSEH_LIB})
add_importlibs(conutils msvcrt kernel32 user32)
