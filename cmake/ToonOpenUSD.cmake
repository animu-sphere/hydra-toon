function(toon_configure_openusd)
  find_package(pxr CONFIG REQUIRED)
  if(NOT TARGET hd OR NOT TARGET plug)
    message(FATAL_ERROR
      "The OpenUSD package must provide the hd and plug CMake targets")
  endif()
  set(PXR_INCLUDE_DIRS "${PXR_INCLUDE_DIRS}" PARENT_SCOPE)
endfunction()