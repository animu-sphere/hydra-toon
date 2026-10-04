include(ToonOpenUSD)
toon_configure_openusd()
find_package(OpenGL REQUIRED) # glf's imported garch link interface

if(NOT TOON_WITH_VULKAN)
  message(FATAL_ERROR
    "TOON_ENABLE_HYDRA2 requires a Vulkan 1.3 SDK and slangc")
endif()
# The co-built runtime is implementation detail of the module. It is not an
# independently packaged OpenStrata plugin bundle or renderer SDK component.
add_library(toon-hydra2-runtime STATIC src/adapter.cpp src/material.cpp)
toon_configure_warnings(toon-hydra2-runtime)
target_include_directories(toon-hydra2-runtime PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(toon-hydra2-runtime
  PUBLIC hd Toon::RenderWorld
  PRIVATE
    glf
    hio
    usdLux
    Toon::RenderExtraction
    Toon::Vulkan
    ${CMAKE_DL_LIBS})
target_compile_features(toon-hydra2-runtime PUBLIC cxx_std_20)
target_compile_definitions(toon-hydra2-runtime PRIVATE
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
  _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)

include(ToonPlugin)
toon_add_plugin(hdToon resources/plugInfo.json.in src/renderer_plugin.cpp)
toon_configure_warnings(hdToon)
target_link_libraries(hdToon PRIVATE toon-hydra2-runtime hd)
target_compile_features(hdToon PRIVATE cxx_std_20)
target_compile_definitions(hdToon PRIVATE
  NOMINMAX
  _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
# Static renderer packs become part of the Hydra module on ELF/Mach-O hosts.
set_target_properties(
  toon-render-world
  toon-render-extraction
  toon-render-vulkan
  toon-hydra2-runtime
  PROPERTIES POSITION_INDEPENDENT_CODE ON)

toon_copy_shaders(hdToon)

toon_install_plugin(hdToon "${TOON_PLUGIN_RESOURCE_FILE}"
  ${TOON_SHADER_SPVS})
install(FILES tests/usdview-smoke.usda
  DESTINATION "${CMAKE_INSTALL_DATADIR}/toon/tests")

if(TOON_BUILD_TESTS)
  add_executable(toon-hydra2-probe tests/plugin_probe.cpp)
  toon_configure_warnings(toon-hydra2-probe)
  target_link_libraries(toon-hydra2-probe PRIVATE hd plug)
  target_compile_features(toon-hydra2-probe PRIVATE cxx_std_20)
  target_compile_definitions(toon-hydra2-probe PRIVATE
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
  add_dependencies(toon-hydra2-probe hdToon)

  # A measurement tool, not a CTest: what it shows depends on which schema
  # and imaging plugins the session registers (MAT-Q1, renderer report 02).
  add_executable(toon-hydra2-material-probe tests/material_probe.cpp)
  toon_configure_warnings(toon-hydra2-material-probe)
  target_link_libraries(toon-hydra2-material-probe
    PRIVATE usdImaging hd usd)
  target_compile_features(toon-hydra2-material-probe PRIVATE cxx_std_20)
  target_compile_definitions(toon-hydra2-material-probe PRIVATE
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)

  # Without arguments, a CTest on a retained scene index that spells the
  # `vrm` locators itself; with --stage, a report of what a real stage's
  # materials select in the session's plugins.
  add_executable(toon-hydra2-material-test tests/material_test.cpp)
  toon_configure_warnings(toon-hydra2-material-test)
  target_link_libraries(toon-hydra2-material-test
    PRIVATE toon-hydra2-runtime usdImaging usd hio)
  target_compile_features(toon-hydra2-material-test PRIVATE cxx_std_20)
  target_compile_definitions(toon-hydra2-material-test PRIVATE
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)

  # A small UsdSkel stage through UsdImaging and usdSkelImaging: what a
  # skinned mesh commits, and what a pose change alone changes.
  add_executable(toon-hydra2-skinning-test tests/skinning_test.cpp)
  toon_configure_warnings(toon-hydra2-skinning-test)
  target_link_libraries(toon-hydra2-skinning-test
    PRIVATE toon-hydra2-runtime usdImaging usd)
  target_compile_features(toon-hydra2-skinning-test PRIVATE cxx_std_20)
  target_compile_definitions(toon-hydra2-skinning-test PRIVATE
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)

  add_executable(toon-hydra2-render-buffer-test
    tests/render_buffer_test.cpp)
  toon_configure_warnings(toon-hydra2-render-buffer-test)
  target_link_libraries(toon-hydra2-render-buffer-test
    PRIVATE toon-hydra2-runtime)
  target_compile_features(toon-hydra2-render-buffer-test PRIVATE cxx_std_20)
  target_compile_definitions(toon-hydra2-render-buffer-test PRIVATE
    NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)

  toon_openusd_root(TOON_PXR_ROOT)
  add_executable(toon-hydra2-light-test tests/light_test.cpp)
  toon_configure_warnings(toon-hydra2-light-test)
  target_link_libraries(toon-hydra2-light-test PRIVATE toon-hydra2-runtime usdImaging usd)
  target_compile_features(toon-hydra2-light-test PRIVATE cxx_std_20)
  target_compile_definitions(toon-hydra2-light-test PRIVATE NOMINMAX
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
  add_test(NAME toon-renderer-hydra-lights COMMAND toon-hydra2-light-test
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/lights.usda")
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
  add_test(NAME toon-renderer-hydra-material
    COMMAND toon-hydra2-material-test)
  add_test(NAME toon-renderer-hydra-skinning
    COMMAND toon-hydra2-skinning-test
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/skinning.usda")
  set_tests_properties(
    toon-renderer-plugin-discovery
    toon-renderer-delegate-creation
    toon-renderer-hydra-render-buffer
    toon-renderer-hydra-material
    toon-renderer-hydra-skinning
    toon-renderer-hydra-lights
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
    # usdview needs OpenGL 4.5 whatever the renderer. A host without it, such
    # as a hosted CI runner, cannot run the smoke at all, so HgiGL's refusal
    # is a SKIP, as a missing display is above; any other failure fails.
    set_tests_properties(toon-renderer-usdview-host PROPERTIES
      LABELS "host-smoke;gpu"
      TIMEOUT 60
      RUN_SERIAL TRUE
      SKIP_REGULAR_EXPRESSION "HgiGL minimum OpenGL[ \t\r\n]+requirements not met")
  else()
    message(STATUS
      "testusdview, matching Python, or a display is unavailable; usdview host evidence will be SKIP")
  endif()
endif()
