function(toon_configure_dependencies)
  set(_toon_with_vulkan OFF)
  if(TOON_ENABLE_VULKAN)
    find_package(Vulkan 1.3 QUIET)
    # Slang is the shader source of truth. The Vulkan SDK bundles slangc since
    # 1.3.296, so one SDK install provides both the loader and the compiler.
    find_program(TOON_SLANGC_EXECUTABLE
      NAMES slangc
      HINTS ENV VULKAN_SDK
      PATH_SUFFIXES bin Bin)
    if(Vulkan_FOUND AND TOON_SLANGC_EXECUTABLE)
      set(_toon_with_vulkan ON)
    elseif(NOT Vulkan_FOUND)
      message(STATUS "Vulkan 1.3 was not found; GPU assertions will report SKIP")
    else()
      message(STATUS
        "slangc was not found (the Vulkan SDK bundles it since 1.3.296); "
        "GPU assertions will report SKIP")
    endif()
  endif()
  set(TOON_WITH_VULKAN "${_toon_with_vulkan}" PARENT_SCOPE)
endfunction()

function(toon_configure_viewport_dependencies)
  find_package(glfw3 3.4 CONFIG QUIET)
  if(NOT TARGET glfw)
    include(FetchContent)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
      GIT_REPOSITORY https://github.com/glfw/glfw.git
      GIT_TAG 7b6aead9fb88b3623e3b3725ebb42670cbe4c579 # 3.4
      GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(glfw)
  endif()

  # Dear ImGui draws the viewport's measurements and debug controls. It is
  # the viewport's alone: it never reaches the backend, whose overlay pass
  # draws the plain OverlayDrawList the viewport converts ImGui's draw data
  # into. The pin follows hydra-merlin's viewport.
  include(FetchContent)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 8936b58fe26e8c3da834b8f60b06511d537b4c63 # 1.92.8
    GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(imgui)
  set(imgui_SOURCE_DIR "${imgui_SOURCE_DIR}" PARENT_SCOPE)

  if(TOON_ENABLE_HYDRA2)
    if(APPLE)
      enable_language(OBJC)
    endif()
    set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NFD_BUILD_SDL2_TESTS OFF CACHE BOOL "" FORCE)
    set(NFD_BUILD_GLFW3_TESTS OFF CACHE BOOL "" FORCE)
    set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
    if(UNIX AND NOT APPLE)
      set(NFD_PORTAL ON CACHE BOOL "" FORCE)
      set(NFD_X11 OFF CACHE BOOL "" FORCE)
      set(NFD_WAYLAND OFF CACHE BOOL "" FORCE)
    endif()
    # Native File Dialog Extended 1.3.0, as pinned by hydra-merlin.
    FetchContent_Declare(nfd
      GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
      GIT_TAG 3cd252a8f7ca32419b1ca235c2990ba6a0ecba7c
      GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(nfd)
    set(nfd_SOURCE_DIR "${nfd_SOURCE_DIR}" PARENT_SCOPE)
  endif()
endfunction()

# The WebGPU backend's Dawn (WebGPU policy §2): a published release of
# prebuilt Windows binaries, pinned by tag and archive SHA-256. Its CMake
# package provides the static dawn::webgpu_dawn, and its tint validates the
# generated WGSL at build time. A pin moves only with a report that reruns
# the parity fixtures. FETCHCONTENT_SOURCE_DIR_DAWN may name an extracted
# copy of the same archive.
function(toon_configure_webgpu_dependencies)
  if(NOT WIN32)
    message(FATAL_ERROR
      "TOON_ENABLE_WEBGPU targets native Windows in v0.7.0 (WebGPU policy §1)")
  endif()
  if(NOT TOON_SLANGC_EXECUTABLE)
    message(FATAL_ERROR
      "TOON_ENABLE_WEBGPU needs slangc, which the Vulkan SDK bundles, to "
      "generate WGSL")
  endif()
  include(FetchContent)
  FetchContent_Declare(dawn
    URL "https://github.com/google/dawn/releases/download/v20261008.181332/Dawn-b73d662f73d57bb42209546910666a38e6db95a1-windows-latest-Release.tar.gz"
    URL_HASH SHA256=b3574efe866329252d738fbb8346e5c1547bdbc23a9db42debfecb267a90cc1e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(dawn)
  find_package(Threads REQUIRED)
  find_package(Dawn CONFIG REQUIRED PATHS "${dawn_SOURCE_DIR}" NO_DEFAULT_PATH)
  find_program(TOON_TINT_EXECUTABLE NAMES tint
    HINTS "${dawn_SOURCE_DIR}/bin" NO_DEFAULT_PATH REQUIRED)
  set(TOON_TINT_EXECUTABLE "${TOON_TINT_EXECUTABLE}" PARENT_SCOPE)

  # Dawn's Direct3D 12 device compiles shaders with DXC, which it loads at
  # run time as dxcompiler.dll beside the executable; its release archive
  # carries none. A published DXC release is pinned the same way, and only
  # dxcompiler.dll, under its LLVM and MIT licences, is copied and
  # installed (report 91). FETCHCONTENT_SOURCE_DIR_DXC may name an
  # extracted copy of the same archive.
  FetchContent_Declare(dxc
    URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip"
    URL_HASH SHA256=ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(dxc)
  set(TOON_DXC_COMPILER "${dxc_SOURCE_DIR}/bin/x64/dxcompiler.dll")
  if(NOT EXISTS "${TOON_DXC_COMPILER}")
    message(FATAL_ERROR "the pinned DXC release has no bin/x64/dxcompiler.dll")
  endif()
  set(TOON_DXC_COMPILER "${TOON_DXC_COMPILER}" PARENT_SCOPE)
  set(TOON_DXC_LICENSES "${dxc_SOURCE_DIR}/LICENSE-LLVM.txt"
    "${dxc_SOURCE_DIR}/LICENCE-MIT.txt" PARENT_SCOPE)
endfunction()

# Tessera for the Tessera host (GPU handoff policy §9, HOST-Q3): its
# repository at a pinned commit, embedded with only its optional Vulkan
# module, linked as tessera::vulkan. The commit is the one that gives
# bind_image its per-binding ImageAlpha (HOST-Q2); a pin moves only with a
# report that reruns the host's smoke. Tessera checks its own slangc version
# and is passed the one this project found. FETCHCONTENT_SOURCE_DIR_TESSERA
# may name a checkout of the same commit.
function(toon_configure_tessera_dependencies)
  if(NOT WIN32)
    message(FATAL_ERROR "TOON_ENABLE_TESSERA builds a Win32 example host")
  endif()
  if(NOT TOON_WITH_VULKAN)
    message(FATAL_ERROR "TOON_ENABLE_TESSERA needs the Vulkan backend and slangc")
  endif()
  include(FetchContent)
  set(TESSERA_BUILD_VULKAN ON CACHE BOOL "" FORCE)
  set(TESSERA_BUILD_FONTS OFF CACHE BOOL "" FORCE)
  set(TESSERA_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(TESSERA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(TESSERA_SLANGC "${TOON_SLANGC_EXECUTABLE}" CACHE FILEPATH "" FORCE)
  FetchContent_Declare(tessera
    GIT_REPOSITORY https://github.com/animu-sphere/tessera.git
    GIT_TAG c999a27a3a5c4ae28c5e68133a1460cc5c63fbdd) # main after #116
  FetchContent_MakeAvailable(tessera)
  set(TOON_TESSERA_SHADERS "${tessera_BINARY_DIR}/backends/vulkan/shaders"
    PARENT_SCOPE)
endfunction()

# Copies what an executable linking the WebGPU backend loads at run time
# next to it: DXC's dxcompiler.dll.
function(toon_copy_webgpu_runtime target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${TOON_DXC_COMPILER}"
      "$<TARGET_FILE_DIR:${target}>"
    VERBATIM)
endfunction()
