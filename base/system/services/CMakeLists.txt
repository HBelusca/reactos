
include_directories(
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/subsys
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl
    ${CMAKE_CURRENT_BINARY_DIR})

add_rpc_files(server ${REACTOS_SOURCE_DIR}/sdk/include/reactos/idl/svcctl.idl)

list(APPEND SOURCE
    config.c
    controlset.c
    database.c
    driver.c
    groupdb.c
    lock.c
    rpcserver.c
    security.c
    services.c
    services.h
    ${CMAKE_CURRENT_BINARY_DIR}/svcctl_s.c)

add_executable(services ${SOURCE} services.rc)
add_pch(services services.h SOURCE)

set_module_type(services win32gui UNICODE)
target_link_libraries(services ${PSEH_LIB})
add_importlibs(services userenv user32 advapi32 advapi32_vista rpcrt4 msvcrt kernel32 ntdll)
add_cd_file(TARGET services DESTINATION reactos/system32 FOR all)
