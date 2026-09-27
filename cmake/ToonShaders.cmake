# Compiles each Slang module's vertex_main and fragment_main into
# <module>.vert.spv and <module>.frag.spv, and sets TOON_SHADER_SPVS in the
# caller's scope to every output, for consumers to copy and install.
function(toon_add_shader_library target)
  set(_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  set(_outputs)
  foreach(source IN LISTS ARGN)
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
        DEPENDS "${_shader_source}"
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
