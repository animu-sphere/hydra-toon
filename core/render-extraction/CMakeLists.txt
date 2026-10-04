add_library(toon-render-extraction STATIC extraction.cpp outline_bounds.cpp outline_occlusion.cpp)
toon_configure_warnings(toon-render-extraction)
add_library(Toon::RenderExtraction ALIAS toon-render-extraction)
set_target_properties(toon-render-extraction PROPERTIES EXPORT_NAME RenderExtraction)
target_include_directories(toon-render-extraction PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(toon-render-extraction PUBLIC Toon::RenderWorld)
target_compile_features(toon-render-extraction PUBLIC cxx_std_20)
