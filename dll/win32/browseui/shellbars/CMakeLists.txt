
PROJECT(SHELL)

add_definitions(-DUNICODE -D_UNICODE)

list(APPEND SOURCE
    CBandSite.cpp
    CBandSiteMenu.cpp
    CBaseBar.cpp
    CISFBand.cpp
    CSHEnumClassesOfCategories.cpp
    shellbars.h)

add_library(shellbars OBJECT ${SOURCE})
add_pch(shellbars shellbars.h SOURCE)

target_link_libraries(shellbars PRIVATE atl_classes)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(shellbars PRIVATE -Wno-unused-but-set-variable)
endif()

add_dependencies(shellbars xdk psdk)
