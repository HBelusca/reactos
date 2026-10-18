
spec2def(mymodule.dll mymodule.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    mymodule.c
    mymodule.h
    ${CMAKE_CURRENT_BINARY_DIR}/mymodule.def)

add_library(mymodule MODULE ${SOURCE})
target_compile_definitions(mymodule PRIVATE _MYMODULE_)
set_module_type(mymodule kerneldll ENTRYPOINT 0)
add_importlibs(mymodule ntoskrnl)
add_dependencies(mymodule psdk)
add_cd_file(TARGET mymodule DESTINATION reactos/system32 NO_CAB FOR all)
