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
