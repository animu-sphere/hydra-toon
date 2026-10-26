set(_toon_mikktspace_dir "${PROJECT_SOURCE_DIR}/third_party/mikktspace")
add_library(toon-render-world STATIC render_world.cpp generated_tangents.cpp
  "${_toon_mikktspace_dir}/mikktspace.c")
# The pinned upstream source is valid C++; isolate its warnings.
set_source_files_properties("${_toon_mikktspace_dir}/mikktspace.c" PROPERTIES LANGUAGE CXX)
if(MSVC)
  set_source_files_properties("${_toon_mikktspace_dir}/mikktspace.c" PROPERTIES COMPILE_OPTIONS /w)
else()
  set_source_files_properties("${_toon_mikktspace_dir}/mikktspace.c" PROPERTIES COMPILE_OPTIONS -w)
endif()
target_include_directories(toon-render-world PRIVATE "${_toon_mikktspace_dir}")
install(FILES "${_toon_mikktspace_dir}/LICENSE" "${_toon_mikktspace_dir}/README.md"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/toon/licenses/mikktspace")
toon_configure_warnings(toon-render-world)
add_library(Toon::RenderWorld ALIAS toon-render-world)
set_target_properties(toon-render-world PROPERTIES EXPORT_NAME RenderWorld)
target_include_directories(toon-render-world PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_features(toon-render-world PUBLIC cxx_std_20)
