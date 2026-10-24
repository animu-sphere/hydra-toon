# Compiles each Slang module's vertex_main and fragment_main for one
# backend, and sets a list of every output in the caller's scope, for
# consumers to copy and install. Sources and the files after INCLUDES are
# paths under the root shaders/ directory, which is also the include root;
# INCLUDES are included by the modules, not compiled, and every module is
# rebuilt when one changes.
#
# By default the outputs are SPIR-V for Vulkan, <module>.vert.spv and
# <module>.frag.spv, listed in TOON_SHADER_SPVS. With WGSL they are WGSL for
# WebGPU, <module>.vert.wgsl and <module>.frag.wgsl, listed in
# TOON_SHADER_WGSLS: compiled against the WebGPU bindings (TOON_WEBGPU) and
# validated by the pinned Dawn release's Tint before they are written, so an
# invalid module fails the build (WebGPU policy §5).
function(toon_add_shader_library target)
  cmake_parse_arguments(PARSE_ARGV 1 _toon "WGSL" "" "INCLUDES")
  set(_root "${PROJECT_SOURCE_DIR}/shaders")
  set(_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  set(_outputs)
  set(_includes)
  foreach(include IN LISTS _toon_INCLUDES)
    list(APPEND _includes "${_root}/${include}")
  endforeach()
  foreach(source IN LISTS _toon_UNPARSED_ARGUMENTS)
    set(_shader_source "${_root}/${source}")
    get_filename_component(_shader_name "${source}" NAME_WE)
    foreach(_stage IN ITEMS vertex fragment)
      if(_stage STREQUAL "vertex")
        set(_suffix vert)
      else()
        set(_suffix frag)
      endif()
      if(_toon_WGSL)
        set(_output "${_shader_dir}/${_shader_name}.${_suffix}.wgsl")
        # Written beside the output and renamed into place only once Tint
        # accepts it, so a failed check leaves no output to look current.
        add_custom_command(
          OUTPUT "${_output}"
          COMMAND "${CMAKE_COMMAND}" -E make_directory "${_shader_dir}"
          COMMAND "${TOON_SLANGC_EXECUTABLE}" "${_shader_source}"
            -I "${_root}" -DTOON_WEBGPU
            -entry "${_stage}_main" -stage "${_stage}" -target wgsl
            -matrix-layout-column-major -warnings-as-errors all
            -o "${_output}.unchecked"
          COMMAND "${TOON_TINT_EXECUTABLE}" --input-format wgsl --format wgsl -o NUL
            "${_output}.unchecked"
          COMMAND "${CMAKE_COMMAND}" -E rename "${_output}.unchecked"
            "${_output}"
          DEPENDS "${_shader_source}" ${_includes}
          VERBATIM)
      else()
        set(_output "${_shader_dir}/${_shader_name}.${_suffix}.spv")
        add_custom_command(
          OUTPUT "${_output}"
          COMMAND "${CMAKE_COMMAND}" -E make_directory "${_shader_dir}"
          COMMAND "${TOON_SLANGC_EXECUTABLE}" "${_shader_source}"
            -I "${_root}"
            -entry "${_stage}_main" -stage "${_stage}" -target spirv
            -matrix-layout-column-major -warnings-as-errors all
            -o "${_output}"
          DEPENDS "${_shader_source}" ${_includes}
          VERBATIM)
      endif()
      list(APPEND _outputs "${_output}")
    endforeach()
  endforeach()
  add_custom_target(${target} DEPENDS ${_outputs})
  if(_toon_WGSL)
    set(TOON_SHADER_WGSLS "${_outputs}" PARENT_SCOPE)
  else()
    set(TOON_SHADER_SPVS "${_outputs}" PARENT_SCOPE)
  endif()
endfunction()

# Copies every compiled shader next to `target`, into its `shaders`
# directory, where the renderer's hosts look for them: the SPIR-V and, for a
# host that offers the WebGPU backend (WGSL) when it is built, the WGSL.
function(toon_copy_shaders target)
  cmake_parse_arguments(PARSE_ARGV 1 _toon "WGSL" "" "")
  set(_shaders)
  if(TARGET toon-renderer-shaders)
    add_dependencies(${target} toon-renderer-shaders)
    list(APPEND _shaders ${TOON_SHADER_SPVS})
  endif()
  if(_toon_WGSL AND TARGET toon-renderer-wgsl)
    add_dependencies(${target} toon-renderer-wgsl)
    list(APPEND _shaders ${TOON_SHADER_WGSLS})
  endif()
  if(NOT _shaders)
    return()
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
      "$<TARGET_FILE_DIR:${target}>/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      ${_shaders}
      "$<TARGET_FILE_DIR:${target}>/shaders"
    VERBATIM)
endfunction()
