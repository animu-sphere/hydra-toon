add_library(toon-render-vulkan STATIC
  vulkan_backend.cpp
  vulkan_present.cpp)
toon_configure_warnings(toon-render-vulkan)
add_library(Toon::Vulkan ALIAS toon-render-vulkan)
set_target_properties(toon-render-vulkan PROPERTIES EXPORT_NAME Vulkan)
target_include_directories(toon-render-vulkan PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(toon-render-vulkan PUBLIC Toon::RenderExtraction)
target_compile_features(toon-render-vulkan PUBLIC cxx_std_20)
if(TOON_WITH_VULKAN)
  target_sources(toon-render-vulkan PRIVATE vulkan_scene.cpp)
  target_link_libraries(toon-render-vulkan PRIVATE Vulkan::Vulkan)
  target_compile_definitions(toon-render-vulkan PRIVATE TOON_HAS_VULKAN=1)

  include(ToonShaders)
  toon_add_shader_library(toon-renderer-shaders shaders/mesh.slang)
  set(TOON_VERTEX_SPV "${TOON_VERTEX_SPV}" PARENT_SCOPE)
  set(TOON_FRAGMENT_SPV "${TOON_FRAGMENT_SPV}" PARENT_SCOPE)
endif()
