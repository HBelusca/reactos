
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/subsys)

add_executable(nativerun nativerun.c ntcrtio.c nativerun.rc)

# Don't use built-ins for (f)printf on GCC/Clang
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(nativerun PRIVATE -fno-builtin-fprintf -fno-builtin-printf)
endif()

## Optional, if you don't want to use ntdll-exported CRT functions
## target_link_libraries(nativerun libcntpr)

##set_module_type(nativerun nativecui)
set_module_type(nativerun win32cui UNICODE ENTRYPOINT NtProcessStartup 4)
add_importlibs(nativerun ntdll)
add_cd_file(TARGET nativerun DESTINATION reactos/system32 FOR all)
