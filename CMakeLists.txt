# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)

project(Toon
  VERSION 0.1.0
  DESCRIPTION "toon — an OpenStrata renderer skeleton"
  LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(TOON_ENABLE_VULKAN "Probe and link the Vulkan backend capability" ON)

# OST-facing build intent. The renderer keeps its project-owned option while
# `ost renderer view` can request the co-built adapter without knowing that
# private option name. A non-empty OST intent synchronizes both adapter options
# so switching intents cannot retain a stale CMake cache value; an empty intent
# leaves direct CMake option overrides in control.
set(OST_RENDERER_ADAPTERS "" CACHE STRING
  "Semicolon-separated renderer adapters requested by OpenStrata")
set(_TOON_MANAGED_ADAPTER_INTENT OFF)
if(NOT OST_RENDERER_ADAPTERS STREQUAL "")
  set(_TOON_MANAGED_ADAPTER_INTENT ON)
endif()
set(_TOON_HYDRA2_DEFAULT OFF)
if("hydra2" IN_LIST OST_RENDERER_ADAPTERS)
  set(_TOON_HYDRA2_DEFAULT ON)
endif()
set(_TOON_VIEWPORT_DEFAULT OFF)
if("viewport" IN_LIST OST_RENDERER_ADAPTERS)
  set(_TOON_VIEWPORT_DEFAULT ON)
endif()
if(_TOON_MANAGED_ADAPTER_INTENT)
  set(TOON_ENABLE_HYDRA2 ${_TOON_HYDRA2_DEFAULT} CACHE BOOL
    "Build the optional co-built OpenUSD Hydra 2 adapter" FORCE)
  set(TOON_ENABLE_VIEWPORT ${_TOON_VIEWPORT_DEFAULT} CACHE BOOL
    "Build the optional GLFW/Vulkan viewport adapter" FORCE)
else()
  option(TOON_ENABLE_HYDRA2 "Build the optional co-built OpenUSD Hydra 2 adapter"
    ${_TOON_HYDRA2_DEFAULT})
  option(TOON_ENABLE_VIEWPORT "Build the optional GLFW/Vulkan viewport adapter"
    ${_TOON_VIEWPORT_DEFAULT})
endif()
option(TOON_BUILD_TESTS "Build renderer contract and install-tree tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(TOON_BUILD_TESTS)
  include(CTest)
endif()

if(MSVC)
  # /utf-8: sources are UTF-8; without it a CP932 (Japanese) host warns C4819.
  add_compile_options(/W4 /permissive- /EHsc /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(TOON_WITH_VULKAN OFF)
if(TOON_ENABLE_VULKAN)
  find_package(Vulkan 1.3 QUIET)
  # Slang is the shader source of truth. The Vulkan SDK bundles slangc since
  # 1.3.296, so one SDK install provides both the loader and the compiler.
  find_program(TOON_SLANGC_EXECUTABLE
    NAMES slangc
    HINTS ENV VULKAN_SDK
    PATH_SUFFIXES bin Bin)
  if(Vulkan_FOUND AND TOON_SLANGC_EXECUTABLE)
    set(TOON_WITH_VULKAN ON)
  elseif(NOT Vulkan_FOUND)
    message(STATUS "Vulkan 1.3 was not found; GPU assertions will report SKIP")
  else()
    message(STATUS
      "slangc was not found (the Vulkan SDK bundles it since 1.3.296); "
      "GPU assertions will report SKIP")
  endif()
endif()

add_subdirectory(core/render-world)
add_subdirectory(core/render-extraction)
add_subdirectory(backend/vulkan)
add_subdirectory(adapters/headless)
if(TOON_ENABLE_HYDRA2)
  add_subdirectory(adapters/hydra2)
  target_compile_definitions(toon-headless PRIVATE TOON_HAS_HYDRA2=1)
endif()
if(TOON_ENABLE_VIEWPORT)
  add_subdirectory(adapters/viewport)
endif()

if(TOON_BUILD_TESTS)
  add_subdirectory(validation)
endif()

# One project-level install graph. Internal source packs are CMake targets, not
# independently versioned packages.
install(TARGETS
    toon-render-world
    toon-render-extraction
    toon-render-vulkan
  EXPORT ToonTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(TARGETS toon-headless
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(TOON_WITH_VULKAN)
  install(FILES "${TOON_VERTEX_SPV}" "${TOON_FRAGMENT_SPV}"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/shaders")
endif()
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

set(Toon_CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Toon")
configure_package_config_file(
  cmake/ToonConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/ToonConfig.cmake"
  INSTALL_DESTINATION "${Toon_CONFIG_INSTALL_DIR}")
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/ToonConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion)
install(EXPORT ToonTargets
  FILE ToonTargets.cmake
  NAMESPACE Toon::
  DESTINATION "${Toon_CONFIG_INSTALL_DIR}")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ToonConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ToonConfigVersion.cmake"
  DESTINATION "${Toon_CONFIG_INSTALL_DIR}")
install(FILES openstrata.renderer.yaml openstrata.scaffold.yaml
  DESTINATION "${CMAKE_INSTALL_DATADIR}/toon")
