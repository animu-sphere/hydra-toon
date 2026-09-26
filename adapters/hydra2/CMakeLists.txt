find_package(pxr CONFIG REQUIRED)

if(NOT TOON_WITH_VULKAN)
  message(FATAL_ERROR
    "TOON_ENABLE_HYDRA2 requires a Vulkan 1.3 SDK and slangc")
endif()
if(NOT TARGET hd OR NOT TARGET plug)
  message(FATAL_ERROR
    "The OpenUSD package must provide the hd and plug CMake targets")
endif()

# The co-built runtime is implementation detail of the module. It is not an
# independently packaged OpenStrata plugin bundle or renderer SDK component.
add_library(toon-hydra2-runtime STATIC src/adapter.cpp)
target_include_directories(toon-hydra2-runtime PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(toon-hydra2-runtime
  PUBLIC hd
  PRIVATE
    Toon::RenderWorld
    Toon::RenderExtraction
    Toon::Vulkan
    ${CMAKE_DL_LIBS})
target_compile_features(toon-hydra2-runtime PUBLIC cxx_std_20)
target_compile_definitions(toon-hydra2-runtime PRIVATE
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
  _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)

add_library(hdToon MODULE
  src/renderer_plugin.cpp
  resources/plugInfo.json.in)
target_link_libraries(hdToon PRIVATE toon-hydra2-runtime hd)
target_compile_features(hdToon PRIVATE cxx_std_20)
target_compile_definitions(hdToon PRIVATE
  NOMINMAX
  _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
set_target_properties(hdToon PROPERTIES PREFIX "")

# Static renderer packs become part of the Hydra module on ELF/Mach-O hosts.
set_target_properties(
  toon-render-world
  toon-render-extraction
  toon-render-vulkan
  toon-hydra2-runtime
  PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(TOON_HYDRA_LIBRARY_NAME
    "hdToon${CMAKE_SHARED_MODULE_SUFFIX}")
configure_file(resources/plugInfo.json.in
  "${CMAKE_CURRENT_BINARY_DIR}/resources/plugInfo.json" @ONLY)

add_dependencies(hdToon toon-renderer-shaders)
add_custom_command(TARGET hdToon POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "$<TARGET_FILE_DIR:hdToon>/resources"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_BINARY_DIR}/resources/plugInfo.json"
    "$<TARGET_FILE_DIR:hdToon>/resources/plugInfo.json"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "$<TARGET_FILE_DIR:hdToon>/shaders"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${TOON_VERTEX_SPV}"
    "$<TARGET_FILE_DIR:hdToon>/shaders/mesh.vert.spv"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${TOON_FRAGMENT_SPV}"
    "$<TARGET_FILE_DIR:hdToon>/shaders/mesh.frag.spv"
  VERBATIM)

set(TOON_HYDRA_INSTALL_DIR
    "${CMAKE_INSTALL_LIBDIR}/usd/hdToon")
install(TARGETS hdToon
  RUNTIME DESTINATION "${TOON_HYDRA_INSTALL_DIR}"
  LIBRARY DESTINATION "${TOON_HYDRA_INSTALL_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/resources/plugInfo.json"
  DESTINATION "${TOON_HYDRA_INSTALL_DIR}/resources")
install(FILES "${TOON_VERTEX_SPV}" "${TOON_FRAGMENT_SPV}"
  DESTINATION "${TOON_HYDRA_INSTALL_DIR}/shaders")
install(FILES tests/usdview-smoke.usda
  DESTINATION "${CMAKE_INSTALL_DATADIR}/toon/tests")

if(TOON_BUILD_TESTS)
  add_executable(toon-hydra2-probe tests/plugin_probe.cpp)
  target_link_libraries(toon-hydra2-probe PRIVATE hd plug)
  target_compile_features(toon-hydra2-probe PRIVATE cxx_std_20)
  target_compile_definitions(toon-hydra2-probe PRIVATE
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
  add_dependencies(toon-hydra2-probe hdToon)

  # A measurement tool, not a CTest: what it shows depends on which schema
  # and imaging plugins the session registers (MAT-Q1, renderer report 02).
  add_executable(toon-hydra2-material-probe tests/material_probe.cpp)
  target_link_libraries(toon-hydra2-material-probe
    PRIVATE usdImaging hd usd)
  target_compile_features(toon-hydra2-material-probe PRIVATE cxx_std_20)
  target_compile_definitions(toon-hydra2-material-probe PRIVATE
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)

  add_executable(toon-hydra2-render-buffer-test
    tests/render_buffer_test.cpp)
  target_link_libraries(toon-hydra2-render-buffer-test
    PRIVATE toon-hydra2-runtime)
  target_compile_features(toon-hydra2-render-buffer-test PRIVATE cxx_std_20)
  target_compile_definitions(toon-hydra2-render-buffer-test PRIVATE
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)

  if(PXR_INCLUDE_DIRS)
    list(GET PXR_INCLUDE_DIRS 0 _toon_pxr_include)
    get_filename_component(TOON_PXR_ROOT
      "${_toon_pxr_include}/.." ABSOLUTE)
  elseif(DEFINED pxr_DIR)
    set(TOON_PXR_ROOT "${pxr_DIR}")
  else()
    message(FATAL_ERROR "Could not derive the OpenUSD runtime root")
  endif()

  set(_toon_pxr_runtime_modifications
    "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin"
    "PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")

  add_test(NAME toon-renderer-plugin-discovery
    COMMAND toon-hydra2-probe --discovery
      "$<TARGET_FILE_DIR:hdToon>/resources")
  add_test(NAME toon-renderer-delegate-creation
    COMMAND toon-hydra2-probe --delegate
      "$<TARGET_FILE_DIR:hdToon>/resources")
  add_test(NAME toon-renderer-hydra-render-buffer
    COMMAND toon-hydra2-render-buffer-test)
  set_tests_properties(
    toon-renderer-plugin-discovery
    toon-renderer-delegate-creation
    toon-renderer-hydra-render-buffer
    PROPERTIES ENVIRONMENT_MODIFICATION
      "${_toon_pxr_runtime_modifications}")

  find_package(Python3 COMPONENTS Interpreter QUIET)
  find_program(TOON_TESTUSDVIEW testusdview
    PATHS "${TOON_PXR_ROOT}/bin" NO_DEFAULT_PATH)
  set(_toon_has_display TRUE)
  if(UNIX AND NOT APPLE AND NOT DEFINED ENV{DISPLAY} AND
     NOT DEFINED ENV{WAYLAND_DISPLAY})
    set(_toon_has_display FALSE)
  endif()
  if(Python3_Interpreter_FOUND AND TOON_TESTUSDVIEW AND
     _toon_has_display)
    add_test(NAME toon-renderer-usdview-host
      COMMAND "${CMAKE_COMMAND}"
        "-DRENDERER_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DRENDERER_CONFIG=$<CONFIG>"
        "-DRENDERER_STAGE_DIR=${CMAKE_CURRENT_BINARY_DIR}/usdview-install"
        "-DRENDERER_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
        "-DRENDERER_INSTALL_DATADIR=${CMAKE_INSTALL_DATADIR}"
        "-DRENDERER_PXR_ROOT=${TOON_PXR_ROOT}"
        "-DRENDERER_PYTHON=${Python3_EXECUTABLE}"
        "-DRENDERER_TESTUSDVIEW=${TOON_TESTUSDVIEW}"
        "-DRENDERER_TEST_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/usdview_smoke_test.py"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_usdview_smoke.cmake")
    set_tests_properties(toon-renderer-usdview-host PROPERTIES
      LABELS "host-smoke;gpu"
      TIMEOUT 60
      RUN_SERIAL TRUE)
  else()
    message(STATUS
      "testusdview, matching Python, or a display is unavailable; usdview host evidence will be SKIP")
  endif()
endif()
