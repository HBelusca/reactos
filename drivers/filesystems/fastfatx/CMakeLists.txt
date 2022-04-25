
list(APPEND SOURCE
    blockdev.c
    cleanup.c
    close.c
    create.c
    dir.c
    direntry.c
    dirwr.c
    ea.c
    fat.c
    fastio.c
    fcb.c
    finfo.c
    flush.c
    fsctl.c
    iface.c
    kdbg.c
    misc.c
    pnp.c
    rw.c
    shutdown.c
    string.c
    volume.c
    vfat.h)

add_library(fastfatx MODULE ${SOURCE} fastfatx.rc)
set_module_type(fastfatx kernelmodedriver)
target_link_libraries(fastfatx ${PSEH_LIB})
add_importlibs(fastfatx ntoskrnl hal)
add_pch(fastfatx vfat.h SOURCE)
add_cd_file(TARGET fastfatx DESTINATION reactos/system32/drivers NO_CAB FOR all)
