
include_directories(dos)

#####################################
# Generate the integrated COMMAND.COM
#
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/command_com.c ${CMAKE_CURRENT_BINARY_DIR}/command_com.h
    COMMAND native-bin2c $<TARGET_PROPERTY:command,BINARY_PATH> ${CMAKE_CURRENT_BINARY_DIR}/command_com.c ${CMAKE_CURRENT_BINARY_DIR}/command_com.h BIN CommandCom
    DEPENDS native-bin2c command)
#####################################

list(APPEND DOS_SOURCE
    dos/dos32krnl/bios.c
    dos/dos32krnl/condrv.c
    dos/dos32krnl/country.c
    dos/dos32krnl/device.c
    dos/dos32krnl/dos.c
    dos/dos32krnl/dosfiles.c
    dos/dos32krnl/emsdrv.c
    dos/dos32krnl/handle.c
    dos/dos32krnl/himem.c
    dos/dos32krnl/memory.c
    dos/dos32krnl/process.c
    dos/dem.c
    dos/mouse32.c
    ${CMAKE_CURRENT_BINARY_DIR}/command_com.c)

add_library(dos ${DOS_SOURCE})
target_link_libraries(dos ${PSEH_LIB})
# add_pch(dos ntvdm.h DOS_SOURCE)
set_module_type(dos module UNICODE)
