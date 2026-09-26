add_library(toon-render-vulkan STATIC
  vulkan_backend.cpp
  vulkan_present.cpp
  vulkan_scene.cpp)
add_library(Toon::Vulkan ALIAS toon-render-vulkan)
set_target_properties(toon-render-vulkan PROPERTIES EXPORT_NAME Vulkan)
target_include_directories(toon-render-vulkan PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(toon-render-vulkan PUBLIC Toon::RenderExtraction)
target_compile_features(toon-render-vulkan PUBLIC cxx_std_20)
if(TOON_WITH_VULKAN)
  target_link_libraries(toon-render-vulkan PRIVATE Vulkan::Vulkan)
  target_compile_definitions(toon-render-vulkan PRIVATE TOON_HAS_VULKAN=1)

  # Slang is the single shader source. Each entry point compiles to its own
  # SPIR-V artifact; slangc names the SPIR-V entry point `main`, which is what
  # the pipeline stage creation expects. The explicit matrix layout keeps
  # host-visible data layouts deterministic once shaders grow parameters.
  set(_shader_source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.slang")
  set(_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  set(TOON_VERTEX_SPV "${_shader_dir}/mesh.vert.spv")
  set(TOON_FRAGMENT_SPV "${_shader_dir}/mesh.frag.spv")
  add_custom_command(
    OUTPUT "${TOON_VERTEX_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_shader_dir}"
    COMMAND "${TOON_SLANGC_EXECUTABLE}" "${_shader_source}"
      -entry vertex_main -stage vertex -target spirv
      -matrix-layout-column-major -warnings-as-errors all
      -o "${TOON_VERTEX_SPV}"
    DEPENDS shaders/mesh.slang
    VERBATIM)
  add_custom_command(
    OUTPUT "${TOON_FRAGMENT_SPV}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_shader_dir}"
    COMMAND "${TOON_SLANGC_EXECUTABLE}" "${_shader_source}"
      -entry fragment_main -stage fragment -target spirv
      -matrix-layout-column-major -warnings-as-errors all
      -o "${TOON_FRAGMENT_SPV}"
    DEPENDS shaders/mesh.slang
    VERBATIM)
  add_custom_target(toon-renderer-shaders
    DEPENDS "${TOON_VERTEX_SPV}" "${TOON_FRAGMENT_SPV}")
  set(TOON_VERTEX_SPV "${TOON_VERTEX_SPV}" PARENT_SCOPE)
  set(TOON_FRAGMENT_SPV "${TOON_FRAGMENT_SPV}" PARENT_SCOPE)
endif()
