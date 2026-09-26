function(toon_add_plugin target resource_template)
  add_library(${target} MODULE ${ARGN} "${resource_template}")
  set_target_properties(${target} PROPERTIES PREFIX "")

  get_filename_component(_resource_name "${resource_template}" NAME_WLE)
  set(_resource_file "${CMAKE_CURRENT_BINARY_DIR}/resources/${_resource_name}")
  set(TOON_PLUGIN_LIBRARY_NAME "${target}${CMAKE_SHARED_MODULE_SUFFIX}")
  configure_file("${resource_template}" "${_resource_file}" @ONLY)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
      "$<TARGET_FILE_DIR:${target}>/resources"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${_resource_file}"
      "$<TARGET_FILE_DIR:${target}>/resources/${_resource_name}"
    VERBATIM)
  set(TOON_PLUGIN_RESOURCE_FILE "${_resource_file}" PARENT_SCOPE)
endfunction()