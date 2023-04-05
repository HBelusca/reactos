
add_subdirectory(beep)
add_subdirectory(bootvid)
add_subdirectory(condrv)

add_subdirectory(kdcom)
add_subdirectory(kdgdb)
add_subdirectory(kdrosdbg)
if(NOT ARCH STREQUAL "arm")
    add_subdirectory(kdvm)
endif()

if(NOT ARCH STREQUAL "arm")
add_subdirectory(nmidebug)
endif()
add_subdirectory(null)
