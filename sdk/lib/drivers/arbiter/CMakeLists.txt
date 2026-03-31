
add_definitions(-D_NTSYSTEM_)

add_library(arbiter STATIC arbiter.c)
add_dependencies(arbiter bugcodes xdk)
target_include_directories(arbiter INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
