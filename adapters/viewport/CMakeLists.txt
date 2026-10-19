# The viewport is an optional native host, not part of the renderer's
# validation contract: composition evidence stays owned by the headless
# adapter. GLFW builds C sources when fetched.
enable_language(C)

if(NOT TOON_WITH_VULKAN)
  message(FATAL_ERROR
    "TOON_ENABLE_VIEWPORT requires a Vulkan 1.3 SDK and slangc")
endif()

toon_configure_viewport_dependencies()

# Dear ImGui's core alone: the viewport feeds it the window's events and
# converts its draw data for the present session's overlay pass, so neither
# of ImGui's platform or renderer backends is compiled.
add_library(toon-viewport-imgui STATIC
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
target_include_directories(toon-viewport-imgui SYSTEM PUBLIC
  "${imgui_SOURCE_DIR}")
target_compile_features(toon-viewport-imgui PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(toon-viewport-imgui PRIVATE /W0)
else()
  target_compile_options(toon-viewport-imgui PRIVATE -w)
endif()

set(_viewport_sources
  src/arguments.cpp
  src/capture.cpp
  src/camera.cpp
  src/main.cpp
  src/overlay.cpp
  src/playback.cpp
  src/skeleton_debug.cpp
  src/telemetry.cpp
  src/window_glfw.cpp)
add_executable(toon-viewport ${_viewport_sources})
toon_configure_warnings(toon-viewport)
target_link_libraries(toon-viewport PRIVATE
  Toon::RenderWorld
  Toon::RenderExtraction
  Toon::Vulkan
  Vulkan::Vulkan
  glfw
  toon-viewport-imgui)
target_compile_features(toon-viewport PRIVATE cxx_std_20)

# With the Hydra adapter in the same build, `--usd <stage>` hosts Hydra in
# the viewport's frame loop through the adapter's runtime library (design
# policy §31). OpenUSD stays in src/hydra_scene.cpp.
if(TOON_ENABLE_HYDRA2)
  include(ToonOpenUSD)
  toon_configure_openusd()
  toon_openusd_root(TOON_PXR_ROOT)
  target_sources(toon-viewport PRIVATE src/hydra_scene.cpp)
  target_link_libraries(toon-viewport PRIVATE
    toon-hydra2-runtime usdImaging usdSkel usdGeom usd nfd)
  target_compile_definitions(toon-viewport PRIVATE
    TOON_VIEWPORT_HAS_HYDRA=1
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
else()
  target_compile_definitions(toon-viewport PRIVATE TOON_VIEWPORT_HAS_HYDRA=0)
endif()

toon_copy_shaders(toon-viewport)

# toon-viewport-avatar: the same viewport as an external host of
# usd-avatar-runtime's real VRM providers (`--avatar-runtime`, roadmap
# v0.3.0). It links the providers and, through them, the VRM and motion
# owners, which toon-viewport does not; it is opt-in and never installed.
if(TOON_ENABLE_HYDRA2 AND TOON_AVATAR_RUNTIME_PROVIDERS)
  find_package(AvatarRuntime CONFIG REQUIRED
    COMPONENTS vrm_expression_usd vrm_lookat_usd vrm_usd motion motion_usd)
  add_executable(toon-viewport-avatar
    src/avatar_runtime.cpp
    src/hydra_scene.cpp
    ${_viewport_sources})
  toon_configure_warnings(toon-viewport-avatar)
  target_link_libraries(toon-viewport-avatar PRIVATE
    Toon::RenderWorld
    Toon::RenderExtraction
    Toon::Vulkan
    Vulkan::Vulkan
    glfw
    toon-viewport-imgui
    toon-hydra2-avatar-binding
    AvatarRuntime::avatarVrmExpressionUsdBinding
    AvatarRuntime::avatarVrmLookAtUsdBinding
    AvatarRuntime::avatarVrmUsdBinding
    AvatarRuntime::avatarMotionAdapter
    AvatarRuntime::avatarMotionUsdBinding
    AvatarRuntime::avatarRuntime
    usdImaging usdSkel usdGeom usd nfd)
  target_compile_features(toon-viewport-avatar PRIVATE cxx_std_20)
  target_compile_definitions(toon-viewport-avatar PRIVATE
    TOON_VIEWPORT_HAS_HYDRA=1
    TOON_VIEWPORT_HAS_AVATAR_RUNTIME=1
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
  toon_copy_shaders(toon-viewport-avatar)
endif()

# The installed tree reuses bin/shaders written by the project install rules.
install(TARGETS toon-viewport
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${PROJECT_SOURCE_DIR}/third_party/stb/LICENSE"
  "${PROJECT_SOURCE_DIR}/third_party/stb/README.md"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/toon/licenses/stb")
# Dear ImGui is MIT-licensed and compiled into the viewport.
install(FILES "${imgui_SOURCE_DIR}/LICENSE.txt"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/toon/licenses/imgui")
if(TOON_ENABLE_HYDRA2)
  install(FILES "${nfd_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/toon/licenses/nativefiledialog-extended")
endif()

if(TOON_BUILD_TESTS)
  add_subdirectory(tests)
endif()
