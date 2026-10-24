toon_configure_webgpu_dependencies()

add_library(toon-render-webgpu STATIC
  webgpu_backend.cpp
  webgpu_device.cpp
  webgpu_scene.cpp)
toon_configure_warnings(toon-render-webgpu)
add_library(Toon::WebGPU ALIAS toon-render-webgpu)
set_target_properties(toon-render-webgpu PROPERTIES EXPORT_NAME WebGPU)
target_include_directories(toon-render-webgpu PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>")
target_link_libraries(toon-render-webgpu
  PUBLIC Toon::RenderExtraction
  PRIVATE Toon::BackendShared dawn::webgpu_dawn)
target_compile_features(toon-render-webgpu PUBLIC cxx_std_20)

include(ToonShaders)
toon_add_shader_library(toon-renderer-wgsl WGSL
  scene/mesh.slang
  preview/preview_surface.slang
  mtoon/mtoon.slang
  mtoon/mtoon_transparent.slang
  mtoon/mtoon_outline.slang
  mmd/mmd.slang
  mmd/mmd_transparent.slang
  mmd/mmd_outline.slang
  webgpu/mtoon_coverage.slang
  webgpu/mipmap.slang
  webgpu/depth_resolve.slang
  INCLUDES bindings/bindings.slang bindings/bindings_vulkan.slang
    bindings/bindings_webgpu.slang common/skinning.slang
    common/surface_common.slang preview/preview_common.slang
    mtoon/mtoon_common.slang mmd/mmd_common.slang)
add_dependencies(toon-render-webgpu toon-renderer-wgsl)
set(TOON_SHADER_WGSLS "${TOON_SHADER_WGSLS}" PARENT_SCOPE)
set(TOON_TINT_EXECUTABLE "${TOON_TINT_EXECUTABLE}" PARENT_SCOPE)
set(TOON_DXC_COMPILER "${TOON_DXC_COMPILER}" PARENT_SCOPE)
set(TOON_DXC_LICENSES "${TOON_DXC_LICENSES}" PARENT_SCOPE)
