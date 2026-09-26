add_library(toon-render-world STATIC render_world.cpp)
add_library(Toon::RenderWorld ALIAS toon-render-world)
set_target_properties(toon-render-world PROPERTIES EXPORT_NAME RenderWorld)
target_include_directories(toon-render-world PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_features(toon-render-world PUBLIC cxx_std_20)
