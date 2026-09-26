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
endfunction()