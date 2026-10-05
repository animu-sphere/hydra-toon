# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)

project(Toon
  VERSION 0.2.0
  DESCRIPTION "toon — an OpenStrata renderer skeleton"
  LANGUAGES CXX)

include(GNUInstallDirs)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ToonDependencies)
include(ToonInstall)
include(ToonWarnings)

option(TOON_ENABLE_VULKAN "Probe and link the Vulkan backend capability" ON)

# OST-facing build intent. `ost renderer view` asks for `hydra2` and
# `ost renderer viewport` for `viewport` without knowing the project-owned
# option names. A requested adapter is added to what the intent's own options
# enable, never the whole set: the `viewport-usd` intent enables the Hydra
# adapter itself, and `ost renderer viewport` asking for the viewport alone
# must not turn it off (ost report 07, Q1).
set(OST_RENDERER_ADAPTERS "" CACHE STRING
  "Semicolon-separated renderer adapters requested by OpenStrata")
option(TOON_ENABLE_HYDRA2 "Build the optional co-built OpenUSD Hydra 2 adapter"
  OFF)
option(TOON_ENABLE_VIEWPORT "Build the optional GLFW/Vulkan viewport adapter"
  OFF)
option(TOON_ENABLE_AVATAR_STATE "Build the optional avatar runtime state consumer" OFF)
if("hydra2" IN_LIST OST_RENDERER_ADAPTERS)
  set(TOON_ENABLE_HYDRA2 ON CACHE BOOL
    "Build the optional co-built OpenUSD Hydra 2 adapter" FORCE)
endif()
if("viewport" IN_LIST OST_RENDERER_ADAPTERS)
  set(TOON_ENABLE_VIEWPORT ON CACHE BOOL
    "Build the optional GLFW/Vulkan viewport adapter" FORCE)
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
if(TOON_ENABLE_AVATAR_STATE)
  add_subdirectory(adapters/fast)
endif()
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
