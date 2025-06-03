
# remove_target_compile_definition
#  Remove one option from the target COMPILE_DEFINITIONS property,
#  previously added with target_compile_definitions(<target> PRIVATE ...)
function(remove_target_compile_definition _module _definition)
    get_target_property(_definitions ${_module} COMPILE_DEFINITIONS)
    list(REMOVE_ITEM _definitions ${_definition})
    set_target_properties(${_module} PROPERTIES COMPILE_DEFINITIONS "${_definitions}")
endfunction()

# remove_target_compile_option
#  Remove one option from the target COMPILE_OPTIONS property,
#  previously added with target_compile_options(<target> PRIVATE ...)
function(remove_target_compile_option _module _option)
    get_target_property(_options ${_module} COMPILE_OPTIONS)
    list(REMOVE_ITEM _options ${_option})
    set_target_properties(${_module} PROPERTIES COMPILE_OPTIONS "${_options}")
endfunction()
