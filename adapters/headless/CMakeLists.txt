add_executable(toon-headless main.cpp)
toon_configure_warnings(toon-headless)
target_link_libraries(toon-headless PRIVATE
  Toon::RenderWorld
  Toon::RenderExtraction
  Toon::Vulkan)
target_compile_features(toon-headless PRIVATE cxx_std_20)

if(TOON_WITH_VULKAN)
  toon_copy_shaders(toon-headless)
endif()

if(NOT CMAKE_CROSSCOMPILING)
  add_custom_command(TARGET toon-headless POST_BUILD
    COMMAND "$<TARGET_FILE:toon-headless>"
      --report "${CMAKE_BINARY_DIR}/renderer-report.json"
    COMMENT "Writing renderer-report.json"
    VERBATIM)
endif()
