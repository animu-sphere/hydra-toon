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

# With the WebGPU backend in the build, `--backend webgpu` presents through
# Dawn (WebGPU policy §8); its WGSL and DXC's runtime sit beside the viewport.
if(TOON_ENABLE_WEBGPU)
  target_link_libraries(toon-viewport PRIVATE Toon::WebGPU)
  # The window's HWND comes through GLFW's native header, which includes
  # windows.h.
  target_compile_definitions(toon-viewport PRIVATE TOON_VIEWPORT_HAS_WEBGPU=1 NOMINMAX)
  toon_copy_shaders(toon-viewport WGSL)
  toon_copy_webgpu_runtime(toon-viewport)
else()
  target_compile_definitions(toon-viewport PRIVATE TOON_VIEWPORT_HAS_WEBGPU=0)
  toon_copy_shaders(toon-viewport)
endif()

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
    TOON_VIEWPORT_HAS_WEBGPU=0
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
  toon_copy_shaders(toon-viewport-avatar)
endif()

if(TOON_VRM_RUNTIME_VIEWPORT)
  find_package(AvatarRuntime CONFIG REQUIRED COMPONENTS vrm_session)
  add_executable(toon-viewport-vrm src/vrm_runtime.cpp src/hydra_scene.cpp ${_viewport_sources})
  toon_configure_warnings(toon-viewport-vrm)
  target_link_libraries(toon-viewport-vrm PRIVATE
    Toon::RenderWorld Toon::RenderExtraction Toon::Vulkan Vulkan::Vulkan glfw toon-viewport-imgui
    toon-hydra2-avatar-binding AvatarRuntime::avatarVrmSession usdImaging usdSkel usdGeom usd nfd)
  target_compile_features(toon-viewport-vrm PRIVATE cxx_std_20)
  target_compile_definitions(toon-viewport-vrm PRIVATE
    TOON_VIEWPORT_HAS_HYDRA=1 TOON_VIEWPORT_HAS_AVATAR_RUNTIME=1 TOON_VIEWPORT_HAS_VRM_RUNTIME=1
    TOON_VIEWPORT_HAS_WEBGPU=0 NOMINMAX _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
  toon_copy_shaders(toon-viewport-vrm)
  set(TOON_VRM_RUNTIME_TEST_STAGE "" CACHE FILEPATH "Local VRM avatar for resident session checks")
  set(TOON_VRM_RUNTIME_TEST_MOTION "" CACHE FILEPATH "Local VRMA clip for resident session checks")
  set(TOON_VRM_RUNTIME_PLUGINS "" CACHE STRING "VRM schema, imaging and format resources for session checks")
  if(TOON_BUILD_TESTS AND TOON_VRM_RUNTIME_TEST_STAGE AND TOON_VRM_RUNTIME_TEST_MOTION AND TOON_VRM_RUNTIME_PLUGINS)
    add_executable(toon-vrm-runtime-test tests/vrm_runtime_test.cpp src/vrm_runtime.cpp src/hydra_scene.cpp src/skeleton_debug.cpp src/telemetry.cpp)
    target_include_directories(toon-vrm-runtime-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(toon-vrm-runtime-test PRIVATE toon-hydra2-avatar-binding
      AvatarRuntime::avatarVrmSession usdImaging usdSkel usdGeom usd)
    target_compile_features(toon-vrm-runtime-test PRIVATE cxx_std_20)
    target_compile_definitions(toon-vrm-runtime-test PRIVATE NOMINMAX _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
    add_test(NAME toon-vrm-runtime-resident COMMAND "${CMAKE_COMMAND}"
      "-DPROGRAM=$<TARGET_FILE:toon-vrm-runtime-test>"
      "-DSTAGE=${TOON_VRM_RUNTIME_TEST_STAGE}" "-DARGUMENTS=${TOON_VRM_RUNTIME_TEST_MOTION}"
      "-DDLLS=$<JOIN:$<TARGET_RUNTIME_DLLS:toon-vrm-runtime-test>,|>"
      "-DPLUGINS=$<JOIN:${TOON_VRM_RUNTIME_PLUGINS},|>"
      -P "${PROJECT_SOURCE_DIR}/adapters/hydra2/tests/run_avatar_providers.cmake")
  endif()
endif()

if(TOON_MMD_RUNTIME_VIEWPORT)
  if(NOT TOON_ENABLE_HYDRA2 OR NOT TARGET toon-hydra2-avatar-binding)
    message(FATAL_ERROR "TOON_MMD_RUNTIME_VIEWPORT requires Hydra and TOON_ENABLE_AVATAR_STATE")
  endif()
  find_package(AvatarRuntime CONFIG REQUIRED COMPONENTS mmd_session)
  add_executable(toon-viewport-mmd src/mmd_runtime.cpp src/hydra_scene.cpp ${_viewport_sources})
  toon_configure_warnings(toon-viewport-mmd)
  target_link_libraries(toon-viewport-mmd PRIVATE
    Toon::RenderWorld Toon::RenderExtraction Toon::Vulkan Vulkan::Vulkan glfw toon-viewport-imgui
    toon-hydra2-avatar-binding AvatarRuntime::avatarMmdSession usdImaging usdSkel usdGeom usd nfd)
  target_compile_features(toon-viewport-mmd PRIVATE cxx_std_20)
  target_compile_definitions(toon-viewport-mmd PRIVATE
    TOON_VIEWPORT_HAS_HYDRA=1 TOON_VIEWPORT_HAS_AVATAR_RUNTIME=1 TOON_VIEWPORT_HAS_MMD_RUNTIME=1
    TOON_VIEWPORT_HAS_WEBGPU=0 NOMINMAX _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
  toon_copy_shaders(toon-viewport-mmd)
  set(TOON_MMD_RUNTIME_TEST_PMX "" CACHE FILEPATH "Generated morph PMX for live viewport acceptance")
  set(TOON_MMD_RUNTIME_TEST_VMD "" CACHE FILEPATH "Generated mixed UV/vertex VMD for live viewport acceptance")
  set(TOON_MMD_RUNTIME_PLUGINS "" CACHE STRING "MMD schema, imaging and file-format plugin directories for live tests")
  if(TOON_BUILD_TESTS AND TOON_MMD_RUNTIME_TEST_PMX AND TOON_MMD_RUNTIME_TEST_VMD AND TOON_MMD_RUNTIME_PLUGINS)
    add_executable(toon-mmd-runtime-test tests/mmd_runtime_test.cpp src/mmd_runtime.cpp src/hydra_scene.cpp src/skeleton_debug.cpp src/telemetry.cpp)
    target_include_directories(toon-mmd-runtime-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(toon-mmd-runtime-test PRIVATE toon-hydra2-avatar-binding
      AvatarRuntime::avatarMmdSession usdImaging usdSkel usdGeom usd)
    target_compile_features(toon-mmd-runtime-test PRIVATE cxx_std_20)
    target_compile_definitions(toon-mmd-runtime-test PRIVATE NOMINMAX _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
    add_test(NAME toon-mmd-runtime-cache COMMAND "${CMAKE_COMMAND}"
      "-DPROGRAM=$<TARGET_FILE:toon-mmd-runtime-test>"
      "-DSTAGE=${TOON_MMD_RUNTIME_TEST_PMX}" "-DARGUMENTS=${TOON_MMD_RUNTIME_TEST_VMD}"
      "-DDLLS=$<JOIN:$<TARGET_RUNTIME_DLLS:toon-mmd-runtime-test>,|>"
      "-DPLUGINS=$<JOIN:${TOON_MMD_RUNTIME_PLUGINS},|>"
      -P "${PROJECT_SOURCE_DIR}/adapters/hydra2/tests/run_avatar_providers.cmake")
    add_test(NAME toon-viewport-mmd-runtime COMMAND "${CMAKE_COMMAND}"
      "-DVIEWPORT=$<TARGET_FILE:toon-viewport-mmd>"
      "-DPMX=${TOON_MMD_RUNTIME_TEST_PMX}" "-DVMD=${TOON_MMD_RUNTIME_TEST_VMD}"
      "-DPLUGINS=$<JOIN:${TOON_MMD_RUNTIME_PLUGINS},|>"
      "-DDLLS=$<JOIN:$<TARGET_RUNTIME_DLLS:toon-viewport-mmd>,|>"
      "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/mmd-runtime-test.log"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_mmd_runtime.cmake")
    set_tests_properties(toon-viewport-mmd-runtime PROPERTIES LABELS "gpu" SKIP_RETURN_CODE 77)
  endif()
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
