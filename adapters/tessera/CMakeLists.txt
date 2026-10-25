# SPDX-License-Identifier: Apache-2.0
# The Tessera host (GPU handoff policy §9): a Win32 example drawing an avatar
# through the GPU handoff and Tessera UI into one swapchain. Built only with
# TOON_ENABLE_TESSERA and the Hydra adapter; never installed.
include(ToonOpenUSD)
toon_configure_openusd()
toon_configure_tessera_dependencies()

add_executable(toon-tessera-host
  src/main.cpp
  src/panel.cpp
  src/compare.cpp
  ../viewport/src/camera.cpp)
toon_configure_warnings(toon-tessera-host)
target_include_directories(toon-tessera-host PRIVATE
  "${PROJECT_SOURCE_DIR}/adapters/hydra2/tests"
  "${PROJECT_SOURCE_DIR}/adapters/viewport/src"
  "${PROJECT_SOURCE_DIR}/validation")
target_link_libraries(toon-tessera-host PRIVATE
  toon-hydra2-runtime usdImaging usdGeom usd Toon::Vulkan Vulkan::Vulkan
  tessera::vulkan)
target_compile_features(toon-tessera-host PRIVATE cxx_std_20)
target_compile_definitions(toon-tessera-host PRIVATE NOMINMAX
  _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
add_dependencies(toon-tessera-host toon-renderer-shaders)

# Tessera's SPIR-V beside the executable; the host reads it and passes the
# words to Tessera.
add_custom_command(TARGET toon-tessera-host POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "$<TARGET_FILE_DIR:toon-tessera-host>/tessera-shaders"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${TOON_TESSERA_SHADERS}/primitive.vertex.spv"
    "${TOON_TESSERA_SHADERS}/primitive.batch.spv"
    "${TOON_TESSERA_SHADERS}/primitive.fragment.spv"
    "${TOON_TESSERA_SHADERS}/primitive.image.spv"
    "$<TARGET_FILE_DIR:toon-tessera-host>/tessera-shaders"
  VERBATIM)

if(TOON_BUILD_TESTS)
  toon_openusd_root(TOON_PXR_ROOT)
  # A brief scripted run on a committed stage: under validation, presented
  # frames against the CPU composite of their capture. It needs a desktop
  # session and a presenting device, and reports SKIP without one.
  add_test(NAME toon-tessera-host-smoke
    COMMAND toon-tessera-host
      --shaders "${CMAKE_BINARY_DIR}/backend/vulkan/shaders"
      --tessera-shaders "$<TARGET_FILE_DIR:toon-tessera-host>/tessera-shaders"
      --stage "${PROJECT_SOURCE_DIR}/adapters/hydra2/tests/skinning.usda"
      --width 640 --height 360 --samples 4
      --smoke "${CMAKE_CURRENT_BINARY_DIR}/smoke")
  set_tests_properties(toon-tessera-host-smoke PROPERTIES
    LABELS "gpu;handoff;tessera" SKIP_RETURN_CODE 77 RESOURCE_LOCK native_window
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
endif()
