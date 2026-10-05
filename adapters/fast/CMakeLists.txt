# SPDX-License-Identifier: Apache-2.0
find_path(TOON_AVATAR_RUNTIME_INCLUDE_DIR avatarRuntime/api.h
  DOC "Directory containing the experimental usd-avatar-runtime C headers")
if(NOT TOON_AVATAR_RUNTIME_INCLUDE_DIR)
  message(FATAL_ERROR "TOON_ENABLE_AVATAR_STATE requires TOON_AVATAR_RUNTIME_INCLUDE_DIR")
endif()
add_library(toon-avatar-state STATIC avatar_state.cpp)
add_library(Toon::AvatarState ALIAS toon-avatar-state)
set_target_properties(toon-avatar-state PROPERTIES EXPORT_NAME AvatarState)
toon_configure_warnings(toon-avatar-state)
target_compile_features(toon-avatar-state PUBLIC cxx_std_20)
target_include_directories(toon-avatar-state PUBLIC
  "$<BUILD_INTERFACE:${TOON_AVATAR_RUNTIME_INCLUDE_DIR}>"
  "$<INSTALL_INTERFACE:$<TARGET_PROPERTY:Toon::AvatarRuntimeHeaders,INTERFACE_INCLUDE_DIRECTORIES>>")
target_link_libraries(toon-avatar-state PUBLIC Toon::RenderExtraction)
install(TARGETS toon-avatar-state EXPORT ToonAvatarStateTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES "${PROJECT_SOURCE_DIR}/include/toon/fast/avatar_state.hpp"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/toon/fast")
install(EXPORT ToonAvatarStateTargets NAMESPACE Toon::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Toon")
if(TOON_BUILD_TESTS)
  add_executable(toon-avatar-state-test avatar_state_test.cpp)
  toon_configure_warnings(toon-avatar-state-test)
  target_link_libraries(toon-avatar-state-test PRIVATE Toon::AvatarState Toon::Vulkan)
  target_link_libraries(toon-avatar-state-test PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME toon-avatar-state COMMAND toon-avatar-state-test)
  configure_file(check-installed.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/check-installed.cmake" @ONLY)
  add_test(NAME toon-avatar-state-installed COMMAND "${CMAKE_COMMAND}"
    "-DCONFIG=$<CONFIG>" -P "${CMAKE_CURRENT_BINARY_DIR}/check-installed.cmake")
  set_tests_properties(toon-avatar-state-installed PROPERTIES RUN_SERIAL TRUE)
  set(TOON_AVATAR_RUNTIME_TEST_LIBRARY "" CACHE FILEPATH "Optional runtime shared library for consumer lifetime tests")
  if(TOON_AVATAR_RUNTIME_TEST_LIBRARY)
    add_test(NAME toon-avatar-state-runtime COMMAND toon-avatar-state-test
      --runtime "${TOON_AVATAR_RUNTIME_TEST_LIBRARY}")
  endif()
  if(TOON_WITH_VULKAN)
    add_dependencies(toon-avatar-state-test toon-renderer-shaders)
    add_test(NAME toon-avatar-state-gpu COMMAND toon-avatar-state-test
      --gpu "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
    set_tests_properties(toon-avatar-state-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu")
  endif()
endif()
