add_library(toon-render-backend-shared STATIC gpu_records.cpp)
toon_configure_warnings(toon-render-backend-shared)
add_library(Toon::BackendShared ALIAS toon-render-backend-shared)
set_target_properties(toon-render-backend-shared PROPERTIES EXPORT_NAME BackendShared)
# Private to the backends: they include its headers by this directory, and
# no consumer of the installed package does.
target_include_directories(toon-render-backend-shared
  INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(toon-render-backend-shared PUBLIC Toon::RenderExtraction)
target_compile_features(toon-render-backend-shared PUBLIC cxx_std_20)
