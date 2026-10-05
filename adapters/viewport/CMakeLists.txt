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
  playback.cpp
  skeleton_debug.cpp
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
    toon-hydra2-runtime usdImaging usdSkel usdGeom usd nfd)
  target_compile_definitions(toon-viewport PRIVATE
    TOON_VIEWPORT_HAS_HYDRA=1
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
else()
  target_compile_definitions(toon-viewport PRIVATE TOON_VIEWPORT_HAS_HYDRA=0)
endif()

toon_copy_shaders(toon-viewport)

# The installed tree reuses bin/shaders written by the project install rules.
install(TARGETS toon-viewport
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
# Dear ImGui is MIT-licensed and compiled into the viewport.
install(FILES "${imgui_SOURCE_DIR}/LICENSE.txt"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/toon/licenses/imgui")
if(TOON_ENABLE_HYDRA2)
  install(FILES "${nfd_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/toon/licenses/nativefiledialog-extended")
endif()

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
  # A sample change must keep an explicitly selected close-up view.
  add_test(NAME toon-viewport-camera-samples
    COMMAND toon-viewport --hidden --frames 8 --vsync off --overlay off
      --camera-pan 13 -7 --camera-dolly 2 --samples 4 --switch-samples 1
      --screenshot "${CMAKE_CURRENT_BINARY_DIR}/camera-samples.ppm")
  add_test(NAME toon-viewport-camera-reference
    COMMAND toon-viewport --hidden --frames 8 --vsync off --overlay off
      --camera-pan 13 -7 --camera-dolly 2 --samples 1
      --screenshot "${CMAKE_CURRENT_BINARY_DIR}/camera-reference.ppm")
  add_test(NAME toon-viewport-camera-samples-image
    COMMAND "${CMAKE_COMMAND}"
      "-DFIRST=${CMAKE_CURRENT_BINARY_DIR}/camera-reference.ppm"
      "-DSECOND=${CMAKE_CURRENT_BINARY_DIR}/camera-samples.ppm"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_captures.cmake")
  set_tests_properties(toon-viewport-camera-samples-image PROPERTIES
    DEPENDS "toon-viewport-camera-reference;toon-viewport-camera-samples"
    SKIP_REGULAR_EXPRESSION "SKIP:"
    LABELS "gpu;viewport;presentation")
  set_tests_properties(toon-viewport-camera-samples toon-viewport-camera-reference
    PROPERTIES LABELS "gpu;viewport;presentation" SKIP_RETURN_CODE 77 TIMEOUT 60)
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
    add_executable(toon-viewport-material-debug-test material_debug_test.cpp hydra_scene.cpp)
    toon_configure_warnings(toon-viewport-material-debug-test)
    target_link_libraries(toon-viewport-material-debug-test PRIVATE
      toon-hydra2-runtime Toon::Vulkan usdImaging usdSkel usdGeom usd)
    target_compile_definitions(toon-viewport-material-debug-test PRIVATE
      NOMINMAX _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
    add_dependencies(toon-viewport-material-debug-test toon-renderer-shaders)
    add_test(NAME toon-viewport-material-debug-state COMMAND toon-viewport-material-debug-test
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/material-debug.usda")
    add_test(NAME toon-viewport-material-debug-gpu COMMAND toon-viewport-material-debug-test
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/material-debug.usda"
      "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
    set_tests_properties(toon-viewport-material-debug-state toon-viewport-material-debug-gpu PROPERTIES
      TIMEOUT 60 LABELS "viewport;hydra"
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
    set_tests_properties(toon-viewport-material-debug-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu;viewport;hydra")
    add_executable(toon-viewport-skeleton-debug-test skeleton_debug_test.cpp hydra_scene.cpp)
    toon_configure_warnings(toon-viewport-skeleton-debug-test)
    target_link_libraries(toon-viewport-skeleton-debug-test PRIVATE
      toon-hydra2-runtime Toon::Vulkan usdImaging usdSkel usdGeom usd)
    target_compile_definitions(toon-viewport-skeleton-debug-test PRIVATE
      NOMINMAX _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
    add_dependencies(toon-viewport-skeleton-debug-test toon-renderer-shaders)
    add_test(NAME toon-viewport-skeleton-debug-state COMMAND toon-viewport-skeleton-debug-test
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/skeleton-debug.usda")
    add_test(NAME toon-viewport-skeleton-debug-gpu COMMAND toon-viewport-skeleton-debug-test
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/skeleton-debug.usda"
      "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
    set_tests_properties(toon-viewport-skeleton-debug-state toon-viewport-skeleton-debug-gpu PROPERTIES
      TIMEOUT 60 LABELS "viewport;hydra"
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
    set_tests_properties(toon-viewport-skeleton-debug-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu;viewport;hydra")
    add_executable(toon-viewport-morph-debug-test morph_debug_test.cpp hydra_scene.cpp)
    toon_configure_warnings(toon-viewport-morph-debug-test)
    target_link_libraries(toon-viewport-morph-debug-test PRIVATE
      toon-hydra2-runtime Toon::Vulkan usdImaging usdSkel usdGeom usd)
    target_compile_definitions(toon-viewport-morph-debug-test PRIVATE
      NOMINMAX _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
    add_dependencies(toon-viewport-morph-debug-test toon-renderer-shaders)
    add_test(NAME toon-viewport-morph-debug-state COMMAND toon-viewport-morph-debug-test
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/morph-motion.usda")
    add_test(NAME toon-viewport-morph-debug-gpu COMMAND toon-viewport-morph-debug-test
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/morph-motion.usda"
      "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
    set_tests_properties(toon-viewport-morph-debug-state toon-viewport-morph-debug-gpu PROPERTIES
      TIMEOUT 60 LABELS "viewport;hydra"
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
    set_tests_properties(toon-viewport-morph-debug-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu;viewport;hydra")
    add_test(NAME toon-viewport-playback
      COMMAND "${CMAKE_COMMAND}"
        "-DVIEWPORT=$<TARGET_FILE:toon-viewport>"
        "-DSCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/morph-motion.usda"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/playback"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_playback.cmake")
    add_test(NAME toon-viewport-latency
      COMMAND "${CMAKE_COMMAND}"
        "-DVIEWPORT=$<TARGET_FILE:toon-viewport>"
        "-DSCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/morph-motion.usda"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/latency"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_latency.cmake")
    set_tests_properties(toon-viewport-latency PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 60
      LABELS "gpu;viewport;presentation;hydra"
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
    set_tests_properties(toon-viewport-playback PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 60
      LABELS "gpu;viewport;presentation;hydra"
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
    add_test(NAME toon-viewport-playback-conflicting-time
      COMMAND toon-viewport --play --usd "${CMAKE_CURRENT_SOURCE_DIR}/tests/morph-motion.usda"
        --time-step 1)
    set_tests_properties(toon-viewport-playback-conflicting-time PROPERTIES
      WILL_FAIL TRUE TIMEOUT 10
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
    add_test(NAME toon-viewport-morph
      COMMAND "${CMAKE_COMMAND}"
        "-DVIEWPORT=$<TARGET_FILE:toon-viewport>"
        "-DSCENE=${CMAKE_CURRENT_SOURCE_DIR}/tests/morph-motion.usda"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/morph-sequence"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_morph.cmake")
    set_tests_properties(toon-viewport-morph PROPERTIES
      SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 60
      LABELS "gpu;viewport;presentation;hydra"
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
    # All-frame capture must retain intermediate animation and its returning
    # image, rather than repeatedly reading a stale final frame.
    add_test(NAME toon-viewport-motion-sequence
      COMMAND toon-viewport --hidden --frames 9 --vsync off --overlay off
        --usd "${CMAKE_CURRENT_SOURCE_DIR}/tests/outline-motion.usda"
        --time 1 --time-step 0.25 --expect-draws 1
        --capture-sequence "${CMAKE_CURRENT_BINARY_DIR}/motion-sequence"
        --camera-output "${CMAKE_CURRENT_BINARY_DIR}/motion-camera.json")
    set_tests_properties(toon-viewport-motion-sequence PROPERTIES
      PASS_REGULAR_EXPRESSION "frames read back: 9"
      LABELS "gpu;viewport;presentation;hydra" SKIP_RETURN_CODE 77 TIMEOUT 60
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
    add_test(NAME toon-viewport-motion-sequence-images
      COMMAND "${CMAKE_COMMAND}"
        "-DSEQUENCE=${CMAKE_CURRENT_BINARY_DIR}/motion-sequence"
        "-DCAMERA=${CMAKE_CURRENT_BINARY_DIR}/motion-camera.json"
        "-DREFERENCE=${CMAKE_CURRENT_BINARY_DIR}/motion-reference.ppm"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_sequence.cmake")
    set_tests_properties(toon-viewport-motion-sequence-images PROPERTIES
      DEPENDS "toon-viewport-motion-sequence;toon-viewport-motion-reference"
      SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "gpu;viewport;presentation;hydra")
    add_test(NAME toon-viewport-motion
      COMMAND toon-viewport --hidden --frames 9 --vsync off --overlay off
        --usd "${CMAKE_CURRENT_SOURCE_DIR}/tests/outline-motion.usda"
        --time 1 --time-step 0.25 --expect-draws 1 --expect-hulls 0
        --screenshot "${CMAKE_CURRENT_BINARY_DIR}/motion.ppm")
    add_test(NAME toon-viewport-motion-reference
      COMMAND toon-viewport --hidden --frames 9 --vsync off --overlay off
        --usd "${CMAKE_CURRENT_SOURCE_DIR}/tests/outline-motion.usda"
        --time 1 --expect-draws 1
        --screenshot "${CMAKE_CURRENT_BINARY_DIR}/motion-reference.ppm")
    set_tests_properties(toon-viewport-motion PROPERTIES
      FAIL_REGULAR_EXPRESSION "toon-viewport:"
      PASS_REGULAR_EXPRESSION
        "Uploads: topology=1 points=1 materials=1 textures=0 skins=1 poses=9")
    set_tests_properties(toon-viewport-motion toon-viewport-motion-reference PROPERTIES
      LABELS "gpu;viewport;presentation;hydra" SKIP_RETURN_CODE 77 TIMEOUT 60
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
    add_test(NAME toon-viewport-motion-return-image
      COMMAND "${CMAKE_COMMAND}"
        "-DFIRST=${CMAKE_CURRENT_BINARY_DIR}/motion-reference.ppm"
        "-DSECOND=${CMAKE_CURRENT_BINARY_DIR}/motion.ppm"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_captures.cmake")
    set_tests_properties(toon-viewport-motion-return-image PROPERTIES
      DEPENDS "toon-viewport-motion;toon-viewport-motion-reference"
      SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "gpu;viewport;presentation;hydra")
    # All three switches use the interactive file-open path. Compare their
    # captures to opening the final stage at startup, including overlapping
    # mesh ids/revisions, changed geometry, up axis and metres per unit.
    file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/replacement.usda"
      "${CMAKE_CURRENT_BINARY_DIR}/モデル.usda" ONLY_IF_DIFFERENT)
    set(_replacement "${CMAKE_CURRENT_BINARY_DIR}/モデル.usda")
    add_test(NAME toon-viewport-file-reference
      COMMAND toon-viewport --hidden --frames 8 --vsync off --overlay off
        --usd "${_replacement}" --expect-draws 1
        --screenshot "${CMAKE_CURRENT_BINARY_DIR}/file-reference.ppm")
    add_test(NAME toon-viewport-file-bootstrap
      COMMAND toon-viewport --hidden --frames 8 --vsync off --overlay off
        --switch-file "${_replacement}" --expect-draws 1
        --screenshot "${CMAKE_CURRENT_BINARY_DIR}/file-bootstrap.ppm")
    add_test(NAME toon-viewport-file-replace
      COMMAND toon-viewport --hidden --frames 8 --vsync off --overlay off
        --usd "${PROJECT_SOURCE_DIR}/adapters/hydra2/tests/usdview-smoke.usda"
        --switch-file "${_replacement}" --expect-draws 1
        --screenshot "${CMAKE_CURRENT_BINARY_DIR}/file-replace.ppm")
    add_test(NAME toon-viewport-file-failed
      COMMAND toon-viewport --hidden --frames 8 --vsync off --overlay off
        --usd "${_replacement}"
        --switch-file "${CMAKE_CURRENT_BINARY_DIR}/missing.usda"
        --expect-draws 1
        --screenshot "${CMAKE_CURRENT_BINARY_DIR}/file-failed.ppm")
    set_tests_properties(toon-viewport-file-failed
      PROPERTIES FAIL_REGULAR_EXPRESSION "Opened file:")
    set_tests_properties(toon-viewport-file-reference toon-viewport-file-bootstrap
      toon-viewport-file-replace toon-viewport-file-failed PROPERTIES
      LABELS "gpu;viewport;presentation;hydra"
      SKIP_RETURN_CODE 77 TIMEOUT 60
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
    foreach(_case bootstrap replace failed)
      add_test(NAME toon-viewport-file-${_case}-image
        COMMAND "${CMAKE_COMMAND}"
          "-DFIRST=${CMAKE_CURRENT_BINARY_DIR}/file-reference.ppm"
          "-DSECOND=${CMAKE_CURRENT_BINARY_DIR}/file-${_case}.ppm"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_captures.cmake")
      set_tests_properties(toon-viewport-file-${_case}-image PROPERTIES
        DEPENDS "toon-viewport-file-reference;toon-viewport-file-${_case}"
        SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "gpu;viewport;presentation;hydra")
    endforeach()
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
