add_executable(toon-headless main.cpp)
target_link_libraries(toon-headless PRIVATE
  Toon::RenderWorld
  Toon::RenderExtraction
  Toon::Vulkan)
target_compile_features(toon-headless PRIVATE cxx_std_20)

if(TOON_WITH_VULKAN)
  add_dependencies(toon-headless toon-renderer-shaders)
  add_custom_command(TARGET toon-headless POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
      "$<TARGET_FILE_DIR:toon-headless>/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${TOON_VERTEX_SPV}"
      "$<TARGET_FILE_DIR:toon-headless>/shaders/mesh.vert.spv"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${TOON_FRAGMENT_SPV}"
      "$<TARGET_FILE_DIR:toon-headless>/shaders/mesh.frag.spv"
    VERBATIM)
endif()

if(NOT CMAKE_CROSSCOMPILING)
  add_custom_command(TARGET toon-headless POST_BUILD
    COMMAND "$<TARGET_FILE:toon-headless>"
      --report "${CMAKE_BINARY_DIR}/renderer-report.json"
    COMMENT "Writing renderer-report.json"
    VERBATIM)
endif()
