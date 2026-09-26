# The viewport is an optional native host, not part of the renderer's
# validation contract: composition evidence stays owned by the headless
# adapter. GLFW builds C sources when fetched.
enable_language(C)

if(NOT TOON_WITH_VULKAN)
  message(FATAL_ERROR
    "TOON_ENABLE_VIEWPORT requires a Vulkan 1.3 SDK and slangc")
endif()

toon_configure_viewport_dependencies()

add_executable(toon-viewport
  main.cpp
  window_glfw.cpp)
toon_configure_warnings(toon-viewport)
target_link_libraries(toon-viewport PRIVATE
  Toon::RenderWorld
  Toon::RenderExtraction
  Toon::Vulkan
  Vulkan::Vulkan
  glfw)
target_compile_features(toon-viewport PRIVATE cxx_std_20)

add_dependencies(toon-viewport toon-renderer-shaders)
add_custom_command(TARGET toon-viewport POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "$<TARGET_FILE_DIR:toon-viewport>/shaders"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${TOON_VERTEX_SPV}"
    "$<TARGET_FILE_DIR:toon-viewport>/shaders/mesh.vert.spv"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${TOON_FRAGMENT_SPV}"
    "$<TARGET_FILE_DIR:toon-viewport>/shaders/mesh.frag.spv"
  VERBATIM)

# The installed tree reuses bin/shaders written by the project install rules.
install(TARGETS toon-viewport
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(TOON_BUILD_TESTS)
  add_test(NAME toon-viewport-present
    COMMAND toon-viewport --hidden --frames 8 --vsync off)
  set_tests_properties(toon-viewport-present PROPERTIES
    LABELS "gpu;viewport;presentation"
    SKIP_RETURN_CODE 77
    TIMEOUT 60)
endif()
