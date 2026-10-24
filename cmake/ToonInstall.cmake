include(CMakePackageConfigHelpers)

function(toon_install_renderer)
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
    install(FILES ${TOON_SHADER_SPVS}
      DESTINATION "${CMAKE_INSTALL_BINDIR}/shaders")
  endif()
  # The WebGPU backend is linked into the headless runner alone, never
  # exported; its WGSL, DXC's dxcompiler.dll and their licences install
  # with it.
  if(TOON_ENABLE_WEBGPU)
    install(FILES ${TOON_SHADER_WGSLS}
      DESTINATION "${CMAKE_INSTALL_BINDIR}/shaders")
    install(FILES "${TOON_DXC_COMPILER}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/third_party/dawn/"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/toon/licenses/dawn")
    install(FILES ${TOON_DXC_LICENSES}
      DESTINATION "${CMAKE_INSTALL_DATADIR}/toon/licenses/dxc")
  endif()
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" PATTERN "fast" EXCLUDE)

  set(_config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/Toon")
  configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/ToonConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/ToonConfig.cmake"
    INSTALL_DESTINATION "${_config_dir}")
  write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/ToonConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
  install(EXPORT ToonTargets
    FILE ToonTargets.cmake
    NAMESPACE Toon::
    DESTINATION "${_config_dir}")
  install(FILES
      "${PROJECT_BINARY_DIR}/ToonConfig.cmake"
      "${PROJECT_BINARY_DIR}/ToonConfigVersion.cmake"
    DESTINATION "${_config_dir}")
  install(FILES
      "${PROJECT_SOURCE_DIR}/openstrata.renderer.yaml"
      "${PROJECT_SOURCE_DIR}/openstrata.scaffold.yaml"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/toon")
endfunction()

function(toon_install_plugin target resource_file)
  set(_install_dir "${CMAKE_INSTALL_LIBDIR}/usd/${target}")
  install(TARGETS ${target}
    RUNTIME DESTINATION "${_install_dir}"
    LIBRARY DESTINATION "${_install_dir}")
  install(FILES "${resource_file}"
    DESTINATION "${_install_dir}/resources")
  install(FILES ${ARGN}
    DESTINATION "${_install_dir}/shaders")
endfunction()
