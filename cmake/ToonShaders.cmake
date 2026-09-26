function(toon_add_shader_library target source)
  get_filename_component(_shader_source "${source}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  get_filename_component(_shader_name "${source}" NAME_WE)
  set(_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
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
    if(_stage STREQUAL "vertex")
      set(TOON_VERTEX_SPV "${_output}" PARENT_SCOPE)
    else()
      set(TOON_FRAGMENT_SPV "${_output}" PARENT_SCOPE)
    endif()
  endforeach()
  add_custom_target(${target} DEPENDS ${_outputs})
endfunction()