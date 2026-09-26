# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)

project(Toon
  VERSION 0.1.0
  DESCRIPTION "toon — an OpenStrata renderer skeleton"
  LANGUAGES CXX)

include(GNUInstallDirs)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ToonDependencies)
include(ToonInstall)
include(ToonWarnings)

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

toon_configure_dependencies()

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

toon_install_renderer()
