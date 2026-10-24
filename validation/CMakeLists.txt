configure_file(
  run-install-smoke.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/run-install-smoke.cmake"
  @ONLY)
configure_file(
  check-core-boundary.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/check-core-boundary.cmake"
  @ONLY)

add_test(NAME toon-renderer-core-boundary
  COMMAND "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_BINARY_DIR}/check-core-boundary.cmake")

add_executable(toon-render-world-test render_world_test.cpp)
toon_configure_warnings(toon-render-world-test)
target_link_libraries(toon-render-world-test PRIVATE Toon::RenderExtraction)
add_test(NAME toon-render-world COMMAND toon-render-world-test)

# Export conversion needs no GPU, window system or OpenUSD runtime.
add_executable(toon-foreground-png-test
  ../adapters/viewport/tests/capture_test.cpp
  ../adapters/viewport/src/capture.cpp)
toon_configure_warnings(toon-foreground-png-test)
target_link_libraries(toon-foreground-png-test PRIVATE Toon::Vulkan)
add_test(NAME toon-foreground-png-state COMMAND toon-foreground-png-test
  "${CMAKE_CURRENT_BINARY_DIR}/foreground.png")
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME toon-foreground-png-decode COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/adapters/viewport/tests/check_png.py"
    --writer "$<TARGET_FILE:toon-foreground-png-test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/png-decode")
endif()

add_executable(toon-playback-test playback_test.cpp
  ../adapters/viewport/src/playback.cpp)
toon_configure_warnings(toon-playback-test)
target_compile_features(toon-playback-test PRIVATE cxx_std_20)
add_test(NAME toon-playback-state COMMAND toon-playback-test)

add_executable(toon-skeleton-projection-test skeleton_projection_test.cpp
  ../adapters/viewport/src/skeleton_debug.cpp ../adapters/viewport/src/camera.cpp)
toon_configure_warnings(toon-skeleton-projection-test)
target_link_libraries(toon-skeleton-projection-test PRIVATE Toon::RenderWorld)
add_test(NAME toon-skeleton-projection COMMAND toon-skeleton-projection-test)

add_executable(toon-late-frame-test late_frame_test.cpp ../adapters/viewport/src/telemetry.cpp)
toon_configure_warnings(toon-late-frame-test)
target_link_libraries(toon-late-frame-test PRIVATE Toon::Vulkan)
add_test(NAME toon-late-frame-state COMMAND toon-late-frame-test)
if(TOON_WITH_VULKAN)
  add_dependencies(toon-late-frame-test toon-renderer-shaders)
  add_test(NAME toon-late-frame-gpu COMMAND toon-late-frame-test "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
  set_tests_properties(toon-late-frame-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu")
endif()

add_executable(toon-morph-test morph_test.cpp)
toon_configure_warnings(toon-morph-test)
target_link_libraries(toon-morph-test PRIVATE Toon::Vulkan)
add_test(NAME toon-morph-state COMMAND toon-morph-test)
add_executable(toon-tangent-test tangent_test.cpp)
toon_configure_warnings(toon-tangent-test)
target_link_libraries(toon-tangent-test PRIVATE Toon::Vulkan)
add_test(NAME toon-tangent-state COMMAND toon-tangent-test)
if(TOON_WITH_VULKAN)
  add_dependencies(toon-tangent-test toon-renderer-shaders)
  add_test(NAME toon-tangent-gpu COMMAND toon-tangent-test
    "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
  set_tests_properties(toon-tangent-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu")
endif()
if(TOON_WITH_VULKAN)
  add_dependencies(toon-morph-test toon-renderer-shaders)
  add_test(NAME toon-morph-gpu COMMAND toon-morph-test
    "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
  set_tests_properties(toon-morph-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu")
endif()

if(TOON_WITH_VULKAN)
  add_executable(toon-subset-test subset_test.cpp)
  toon_configure_warnings(toon-subset-test)
  target_link_libraries(toon-subset-test PRIVATE Toon::Vulkan)
  add_dependencies(toon-subset-test toon-renderer-shaders)
  add_test(NAME toon-subset-gpu COMMAND toon-subset-test
    "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
  set_tests_properties(toon-subset-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu")
  add_executable(toon-preview-surface-test preview_surface_test.cpp)
  toon_configure_warnings(toon-preview-surface-test)
  target_link_libraries(toon-preview-surface-test PRIVATE Toon::Vulkan)
  add_dependencies(toon-preview-surface-test toon-renderer-shaders)
  add_test(NAME toon-preview-surface-gpu COMMAND toon-preview-surface-test
    "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
  set_tests_properties(toon-preview-surface-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu")
  if(TOON_ENABLE_WEBGPU)
    # The same PreviewSurface checks on the WebGPU backend (WebGPU policy §9).
    target_link_libraries(toon-preview-surface-test PRIVATE Toon::WebGPU)
    target_compile_definitions(toon-preview-surface-test PRIVATE TOON_HAS_WEBGPU=1)
    toon_copy_webgpu_runtime(toon-preview-surface-test)
    add_test(NAME toon-preview-surface-webgpu COMMAND toon-preview-surface-test
      "${CMAKE_BINARY_DIR}/backend/webgpu/shaders" --backend webgpu)
    set_tests_properties(toon-preview-surface-webgpu PROPERTIES
      SKIP_RETURN_CODE 77 LABELS "gpu;webgpu")
  endif()
  add_executable(toon-rgba-output-test rgba_output_test.cpp)
  toon_configure_warnings(toon-rgba-output-test)
  target_link_libraries(toon-rgba-output-test PRIVATE Toon::Vulkan)
  add_dependencies(toon-rgba-output-test toon-renderer-shaders)
  add_test(NAME toon-rgba-output-gpu COMMAND toon-rgba-output-test
    "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
  set_tests_properties(toon-rgba-output-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu")
  add_executable(toon-rgba-fixture-test rgba_fixture_test.cpp
    ../adapters/viewport/src/capture.cpp)
  toon_configure_warnings(toon-rgba-fixture-test)
  target_link_libraries(toon-rgba-fixture-test PRIVATE Toon::Vulkan)
  add_dependencies(toon-rgba-fixture-test toon-renderer-shaders)
  if(Python3_Interpreter_FOUND)
    add_test(NAME toon-rgba-fixtures-gpu COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/scripts/check_rgba_fixtures.py"
      --capture "$<TARGET_FILE:toon-rgba-fixture-test>"
      --shaders "${CMAKE_BINARY_DIR}/backend/vulkan/shaders"
      --output "${CMAKE_CURRENT_BINARY_DIR}/rgba-fixtures")
    set_tests_properties(toon-rgba-fixtures-gpu PROPERTIES
      SKIP_RETURN_CODE 77 LABELS "gpu;rgba" TIMEOUT 90)
  endif()
endif()

# The headless evidence run on the WebGPU backend (WebGPU policy §8); its
# report sits beside the Vulkan one the build writes.
if(TOON_ENABLE_WEBGPU)
  add_test(NAME toon-headless-webgpu COMMAND toon-headless --backend webgpu
    --report "${CMAKE_CURRENT_BINARY_DIR}/renderer-report-webgpu.json")
  set_tests_properties(toon-headless-webgpu PROPERTIES LABELS "gpu;webgpu")
endif()

# Vulkan/WebGPU parity on the canonical scenes (WebGPU policy §9, WGPU-Q1):
# the summary and, per scene, both backends' images and their difference.
if(TOON_ENABLE_WEBGPU AND TOON_WITH_VULKAN)
  add_executable(toon-webgpu-parity-test webgpu_parity_test.cpp
    ../adapters/viewport/src/capture.cpp)
  toon_configure_warnings(toon-webgpu-parity-test)
  target_include_directories(toon-webgpu-parity-test PRIVATE
    "${PROJECT_SOURCE_DIR}/adapters/viewport/src"
    "${PROJECT_SOURCE_DIR}/third_party/stb")
  target_link_libraries(toon-webgpu-parity-test PRIVATE Toon::Vulkan Toon::WebGPU)
  add_dependencies(toon-webgpu-parity-test toon-renderer-shaders toon-renderer-wgsl)
  toon_copy_webgpu_runtime(toon-webgpu-parity-test)
  add_test(NAME toon-webgpu-parity COMMAND toon-webgpu-parity-test
    "${CMAKE_BINARY_DIR}/backend/vulkan/shaders"
    "${CMAKE_BINARY_DIR}/backend/webgpu/shaders"
    "${CMAKE_CURRENT_BINARY_DIR}/webgpu-parity.json"
    --images "${CMAKE_CURRENT_BINARY_DIR}/webgpu-parity")
  set_tests_properties(toon-webgpu-parity PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "gpu;webgpu" TIMEOUT 120)
endif()

add_executable(toon-expression-test expression_test.cpp)
toon_configure_warnings(toon-expression-test)
target_link_libraries(toon-expression-test PRIVATE Toon::Vulkan)
add_test(NAME toon-expression-state COMMAND toon-expression-test)
if(TOON_WITH_VULKAN)
  add_dependencies(toon-expression-test toon-renderer-shaders)
  add_test(NAME toon-expression-gpu COMMAND toon-expression-test
    "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
  set_tests_properties(toon-expression-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu")
endif()

add_executable(toon-outline-bounds-test outline_bounds_test.cpp)
toon_configure_warnings(toon-outline-bounds-test)
target_link_libraries(toon-outline-bounds-test PRIVATE Toon::RenderExtraction)
add_test(NAME toon-outline-bounds COMMAND toon-outline-bounds-test)
add_executable(toon-shadow-fit-test shadow_fit_test.cpp)
toon_configure_warnings(toon-shadow-fit-test)
target_link_libraries(toon-shadow-fit-test PRIVATE Toon::RenderExtraction)
add_test(NAME toon-shadow-fit COMMAND toon-shadow-fit-test)

add_executable(toon-outline-occlusion-test outline_occlusion_test.cpp)
toon_configure_warnings(toon-outline-occlusion-test)
target_link_libraries(toon-outline-occlusion-test PRIVATE Toon::RenderExtraction)
add_test(NAME toon-outline-occlusion COMMAND toon-outline-occlusion-test)

add_test(NAME toon-renderer-evidence
  COMMAND "$<TARGET_FILE:toon-headless>"
    --report "${CMAKE_BINARY_DIR}/renderer-ctest-report.json")

add_test(NAME toon-renderer-install-tree
  COMMAND "${CMAKE_COMMAND}"
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
    "-DCONFIG=$<CONFIG>"
    "-DPREFIX=${CMAKE_BINARY_DIR}/renderer-install"
    -P "${CMAKE_CURRENT_BINARY_DIR}/run-install-smoke.cmake")
set_tests_properties(toon-renderer-install-tree PROPERTIES RUN_SERIAL TRUE)
