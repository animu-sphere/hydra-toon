# Compiles each Slang module's vertex_main and fragment_main into
# <module>.vert.spv and <module>.frag.spv, and sets TOON_SHADER_SPVS in the
# caller's scope to every output, for consumers to copy and install. Files
# after INCLUDES are included by the modules, not compiled; every module is
# rebuilt when one changes.
function(toon_add_shader_library target)
  cmake_parse_arguments(PARSE_ARGV 1 _toon "" "" "INCLUDES")
  set(_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  set(_outputs)
  set(_includes)
  foreach(include IN LISTS _toon_INCLUDES)
    get_filename_component(_include "${include}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND _includes "${_include}")
  endforeach()
  foreach(source IN LISTS _toon_UNPARSED_ARGUMENTS)
    get_filename_component(_shader_source "${source}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(_shader_name "${source}" NAME_WE)
    foreach(_stage IN ITEMS vertex fragment)
      if(_stage STREQUAL "vertex")
        set(_suffix vert)
      else()
        set(_suffix frag)
      endif()
      set(_output "${_shader_dir}/${_shader_name}.${_suffix}.spv")
      add_custom_command(
        OUTPUT "${_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_shader_dir}"
        COMMAND "${TOON_SLANGC_EXECUTABLE}" "${_shader_source}"
          -entry "${_stage}_main" -stage "${_stage}" -target spirv
          -matrix-layout-column-major -warnings-as-errors all
          -o "${_output}"
        DEPENDS "${_shader_source}" ${_includes}
        VERBATIM)
      list(APPEND _outputs "${_output}")
    endforeach()
  endforeach()
  add_custom_target(${target} DEPENDS ${_outputs})
  set(TOON_SHADER_SPVS "${_outputs}" PARENT_SCOPE)
endfunction()

# Copies every compiled shader next to `target`, into its `shaders`
# directory, where the renderer's hosts look for them.
function(toon_copy_shaders target)
  add_dependencies(${target} toon-renderer-shaders)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
      "$<TARGET_FILE_DIR:${target}>/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      ${TOON_SHADER_SPVS}
      "$<TARGET_FILE_DIR:${target}>/shaders"
    VERBATIM)
endfunction()
