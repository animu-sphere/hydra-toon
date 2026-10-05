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

add_executable(toon-playback-test playback_test.cpp
  ../adapters/viewport/playback.cpp)
toon_configure_warnings(toon-playback-test)
target_compile_features(toon-playback-test PRIVATE cxx_std_20)
add_test(NAME toon-playback-state COMMAND toon-playback-test)

add_executable(toon-late-frame-test late_frame_test.cpp ../adapters/viewport/telemetry.cpp)
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
if(TOON_WITH_VULKAN)
  add_dependencies(toon-morph-test toon-renderer-shaders)
  add_test(NAME toon-morph-gpu COMMAND toon-morph-test
    "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
  set_tests_properties(toon-morph-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu")
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
