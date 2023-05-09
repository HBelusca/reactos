
if(MSVC)
    # Explicitly use string pooling
    add_compile_flags("/GF")
endif()

include_directories(BEFORE include)

add_definitions(-D_NTHAL_ -D_BLDR_ -D_NTSYSTEM_)

##
## A simple demonstration PE image :)
##
list(APPEND SOURCE osloader.c)
add_executable(osloader ${SOURCE})
target_link_libraries(osloader libcntpr)
## target_link_libraries(osloader cportlib blcmlib blrtl libcntpr)

if(MSVC)
    # We don't need hotpatching
    remove_target_compile_option(osloader "/hotpatch")
endif()

if(MSVC)
    target_link_options(osloader PRIVATE /ignore:4078 /ignore:4254 /DRIVER)
else()
    target_link_options(osloader PRIVATE "-Wl,--strip-all,--exclude-all-symbols")
endif()

# dynamic analysis switches
if(STACK_PROTECTOR)
    target_sources(osloader PRIVATE $<TARGET_OBJECTS:gcc_ssp_nt>)
endif()

if(RUNTIME_CHECKS)
    target_link_libraries(osloader runtmchk)
    ## target_link_options(osloader PRIVATE "/MERGE:.rtc=.text")
endif()

# set_image_base(osloader 0x00400000) ## Just keep the default.
set_subsystem(osloader native)
set_entrypoint(osloader NtProcessStartup 4) ## This is in principle the default entry point for native apps.

add_cd_file(TARGET osloader DESTINATION loader NO_CAB NOT_IN_HYBRIDCD FOR bootcd livecd hybridcd)
