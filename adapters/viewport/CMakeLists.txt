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

add_executable(toon-viewport
  camera.cpp
  main.cpp
  overlay.cpp
  telemetry.cpp
  window_glfw.cpp)
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
# policy §31). OpenUSD stays in hydra_scene.cpp.
if(TOON_ENABLE_HYDRA2)
  include(ToonOpenUSD)
  toon_configure_openusd()
  toon_openusd_root(TOON_PXR_ROOT)
  target_sources(toon-viewport PRIVATE hydra_scene.cpp)
  target_link_libraries(toon-viewport PRIVATE
    toon-hydra2-runtime usdImaging usdGeom usd)
  target_compile_definitions(toon-viewport PRIVATE
    TOON_VIEWPORT_HAS_HYDRA=1
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
endif()

toon_copy_shaders(toon-viewport)

# The installed tree reuses bin/shaders written by the project install rules.
install(TARGETS toon-viewport
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
# Dear ImGui is MIT-licensed and compiled into the viewport.
install(FILES "${imgui_SOURCE_DIR}/LICENSE.txt"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/toon/licenses/imgui")

if(TOON_BUILD_TESTS)
  add_test(NAME toon-viewport-present
    COMMAND toon-viewport --hidden --frames 8 --vsync off)
  # A capture reads back the frame it asks for and no other.
  add_test(NAME toon-viewport-capture
    COMMAND toon-viewport --hidden --frames 4 --vsync off
      --screenshot "${CMAKE_CURRENT_BINARY_DIR}/toon-viewport-capture.ppm")
  # The sample count changes while the viewport runs, 4 to 1 halfway, as
  # the number keys change it: the pipelines and targets are rebuilt once,
  # and validation stays clean across the change.
  add_test(NAME toon-viewport-samples
    COMMAND toon-viewport --hidden --frames 8 --vsync off --samples 4
      --switch-samples 1)
  # The same capture without the overlay: the two files must be the same
  # image, since the overlay is drawn after the copy.
  add_test(NAME toon-viewport-capture-no-overlay
    COMMAND toon-viewport --hidden --frames 4 --vsync off --overlay off
      --screenshot
        "${CMAKE_CURRENT_BINARY_DIR}/toon-viewport-capture-no-overlay.ppm")
  add_test(NAME toon-viewport-capture-without-overlay
    COMMAND "${CMAKE_COMMAND}"
      "-DFIRST=${CMAKE_CURRENT_BINARY_DIR}/toon-viewport-capture.ppm"
      "-DSECOND=${CMAKE_CURRENT_BINARY_DIR}/toon-viewport-capture-no-overlay.ppm"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_captures.cmake")
  set_tests_properties(toon-viewport-capture-without-overlay PROPERTIES
    DEPENDS "toon-viewport-capture;toon-viewport-capture-no-overlay"
    SKIP_REGULAR_EXPRESSION "SKIP:"
    LABELS "gpu;viewport;presentation")
  set_tests_properties(toon-viewport-present toon-viewport-capture
    toon-viewport-samples toon-viewport-capture-no-overlay PROPERTIES
    LABELS "gpu;viewport;presentation"
    SKIP_RETURN_CODE 77
    TIMEOUT 60)
  if(TOON_ENABLE_HYDRA2)
    # The usdview smoke stage through Hydra: its one mesh must draw.
    add_test(NAME toon-viewport-present-usd
      COMMAND toon-viewport --hidden --frames 8 --vsync off
        --usd "${PROJECT_SOURCE_DIR}/adapters/hydra2/tests/usdview-smoke.usda"
        --expect-draws 1)
    set_tests_properties(toon-viewport-present-usd PROPERTIES
      LABELS "gpu;viewport;presentation;hydra"
      SKIP_RETURN_CODE 77
      TIMEOUT 60
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
  endif()
endif()
