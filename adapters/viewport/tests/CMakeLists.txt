add_executable(toon-viewport-composite-test composite_test.cpp)
toon_configure_warnings(toon-viewport-composite-test)
target_link_libraries(toon-viewport-composite-test PRIVATE Toon::Vulkan Vulkan::Vulkan glfw)
add_dependencies(toon-viewport-composite-test toon-renderer-shaders)
add_test(NAME toon-viewport-composite-gpu COMMAND toon-viewport-composite-test
  "${CMAKE_BINARY_DIR}/backend/vulkan/shaders" "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(toon-viewport-composite-gpu PROPERTIES
  LABELS "gpu;viewport;presentation;rgba" SKIP_RETURN_CODE 77 TIMEOUT 60)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME toon-viewport-foreground-png COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_png.py"
    --viewport "$<TARGET_FILE:toon-viewport>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/png-export")
  set_tests_properties(toon-viewport-foreground-png PROPERTIES
    LABELS "gpu;viewport;presentation;rgba" SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
add_test(NAME toon-viewport-foreground-needs-frames
  COMMAND toon-viewport --foreground-output foreground.png)
set_tests_properties(toon-viewport-foreground-needs-frames PROPERTIES WILL_FAIL TRUE)
add_test(NAME toon-viewport-present
  COMMAND toon-viewport --hidden --frames 8 --vsync off)
# A capture reads back the frame it asks for and no other.
add_test(NAME toon-viewport-capture
  COMMAND toon-viewport --hidden --frames 4 --vsync off
    --screenshot "${CMAKE_CURRENT_BINARY_DIR}/toon-viewport-capture.ppm")
# The sample count changes while the viewport runs, 4 to 1 halfway, as
# the number keys change it: the pipelines and targets are rebuilt once,
# and validation stays clean across the change.
add_test(NAME toon-viewport-samples
  COMMAND toon-viewport --hidden --frames 8 --vsync off --samples 4
    --switch-samples 1)
# A sample change must keep an explicitly selected close-up view.
add_test(NAME toon-viewport-camera-samples
  COMMAND toon-viewport --hidden --frames 8 --vsync off --overlay off
    --camera-pan 13 -7 --camera-dolly 2 --samples 4 --switch-samples 1
    --screenshot "${CMAKE_CURRENT_BINARY_DIR}/camera-samples.ppm")
add_test(NAME toon-viewport-camera-reference
  COMMAND toon-viewport --hidden --frames 8 --vsync off --overlay off
    --camera-pan 13 -7 --camera-dolly 2 --samples 1
    --screenshot "${CMAKE_CURRENT_BINARY_DIR}/camera-reference.ppm")
add_test(NAME toon-viewport-camera-samples-image
  COMMAND "${CMAKE_COMMAND}"
    "-DFIRST=${CMAKE_CURRENT_BINARY_DIR}/camera-reference.ppm"
    "-DSECOND=${CMAKE_CURRENT_BINARY_DIR}/camera-samples.ppm"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_captures.cmake")
set_tests_properties(toon-viewport-camera-samples-image PROPERTIES
  DEPENDS "toon-viewport-camera-reference;toon-viewport-camera-samples"
  SKIP_REGULAR_EXPRESSION "SKIP:"
  LABELS "gpu;viewport;presentation")
set_tests_properties(toon-viewport-camera-samples toon-viewport-camera-reference
  PROPERTIES LABELS "gpu;viewport;presentation" SKIP_RETURN_CODE 77 TIMEOUT 60)
# The same capture without the overlay: the two files must be the same
# image, since the overlay is drawn after the copy.
add_test(NAME toon-viewport-capture-no-overlay
  COMMAND toon-viewport --hidden --frames 4 --vsync off --overlay off
    --screenshot
      "${CMAKE_CURRENT_BINARY_DIR}/toon-viewport-capture-no-overlay.ppm")
add_test(NAME toon-viewport-capture-without-overlay
  COMMAND "${CMAKE_COMMAND}"
    "-DFIRST=${CMAKE_CURRENT_BINARY_DIR}/toon-viewport-capture.ppm"
    "-DSECOND=${CMAKE_CURRENT_BINARY_DIR}/toon-viewport-capture-no-overlay.ppm"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_captures.cmake")
set_tests_properties(toon-viewport-capture-without-overlay PROPERTIES
  DEPENDS "toon-viewport-capture;toon-viewport-capture-no-overlay"
  SKIP_REGULAR_EXPRESSION "SKIP:"
  LABELS "gpu;viewport;presentation")
set_tests_properties(toon-viewport-present toon-viewport-capture
  toon-viewport-samples toon-viewport-capture-no-overlay PROPERTIES
  LABELS "gpu;viewport;presentation"
  SKIP_RETURN_CODE 77
  TIMEOUT 60)
# The same presentation through the WebGPU backend (WebGPU policy §8): frames,
# a capture without the overlay, a live sample change, and the presented
# image of one view, at one sample and four, against the Vulkan session's.
if(TOON_ENABLE_WEBGPU)
  add_test(NAME toon-viewport-webgpu-present
    COMMAND toon-viewport --backend webgpu --hidden --frames 8 --vsync off)
  add_test(NAME toon-viewport-webgpu-capture
    COMMAND toon-viewport --backend webgpu --hidden --frames 4 --vsync off
      --screenshot "${CMAKE_CURRENT_BINARY_DIR}/webgpu-capture.ppm")
  add_test(NAME toon-viewport-webgpu-capture-no-overlay
    COMMAND toon-viewport --backend webgpu --hidden --frames 4 --vsync off --overlay off
      --screenshot "${CMAKE_CURRENT_BINARY_DIR}/webgpu-capture-no-overlay.ppm")
  add_test(NAME toon-viewport-webgpu-capture-without-overlay
    COMMAND "${CMAKE_COMMAND}"
      "-DFIRST=${CMAKE_CURRENT_BINARY_DIR}/webgpu-capture.ppm"
      "-DSECOND=${CMAKE_CURRENT_BINARY_DIR}/webgpu-capture-no-overlay.ppm"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_captures.cmake")
  set_tests_properties(toon-viewport-webgpu-capture-without-overlay PROPERTIES
    DEPENDS "toon-viewport-webgpu-capture;toon-viewport-webgpu-capture-no-overlay")
  add_test(NAME toon-viewport-webgpu-samples
    COMMAND toon-viewport --backend webgpu --hidden --frames 8 --vsync off --samples 4
      --switch-samples 1)
  if(Python3_Interpreter_FOUND)
    add_test(NAME toon-viewport-webgpu-foreground-png COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_png.py"
      --viewport "$<TARGET_FILE:toon-viewport>" --backend webgpu
      --output "${CMAKE_CURRENT_BINARY_DIR}/webgpu-png-export")
    set_tests_properties(toon-viewport-webgpu-foreground-png PROPERTIES
      LABELS "gpu;viewport;presentation;rgba;webgpu" SKIP_RETURN_CODE 77 TIMEOUT 120)
  endif()
  set(_toon_parity_tests)
  foreach(_toon_samples IN ITEMS 1 4)
    foreach(_toon_backend IN ITEMS vulkan webgpu)
      add_test(NAME toon-viewport-parity-${_toon_backend}-${_toon_samples}
        COMMAND toon-viewport --backend ${_toon_backend} --hidden --frames 8 --vsync off
          --overlay off --camera-pan 13 -7 --camera-dolly 2 --samples ${_toon_samples}
          --screenshot
            "${CMAKE_CURRENT_BINARY_DIR}/parity-${_toon_backend}-${_toon_samples}.ppm")
      list(APPEND _toon_parity_tests toon-viewport-parity-${_toon_backend}-${_toon_samples})
    endforeach()
    add_test(NAME toon-viewport-webgpu-parity-${_toon_samples}
      COMMAND "${CMAKE_COMMAND}"
        "-DFIRST=${CMAKE_CURRENT_BINARY_DIR}/parity-vulkan-${_toon_samples}.ppm"
        "-DSECOND=${CMAKE_CURRENT_BINARY_DIR}/parity-webgpu-${_toon_samples}.ppm"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_captures.cmake")
    set_tests_properties(toon-viewport-webgpu-parity-${_toon_samples} PROPERTIES
      DEPENDS "toon-viewport-parity-vulkan-${_toon_samples};toon-viewport-parity-webgpu-${_toon_samples}")
  endforeach()
  set_tests_properties(toon-viewport-webgpu-present toon-viewport-webgpu-capture
    toon-viewport-webgpu-capture-no-overlay toon-viewport-webgpu-samples
    ${_toon_parity_tests} PROPERTIES
    LABELS "gpu;viewport;presentation;webgpu" SKIP_RETURN_CODE 77 TIMEOUT 60)
  set_tests_properties(toon-viewport-webgpu-capture-without-overlay
    toon-viewport-webgpu-parity-1 toon-viewport-webgpu-parity-4 PROPERTIES
    SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "gpu;viewport;presentation;webgpu")
endif()
if(TOON_ENABLE_HYDRA2)
  add_executable(toon-viewport-material-debug-test material_debug_test.cpp ../src/hydra_scene.cpp)
  toon_configure_warnings(toon-viewport-material-debug-test)
  target_include_directories(toon-viewport-material-debug-test PRIVATE ../src)
  target_link_libraries(toon-viewport-material-debug-test PRIVATE
    toon-hydra2-runtime Toon::Vulkan usdImaging usdSkel usdGeom usd)
  target_compile_definitions(toon-viewport-material-debug-test PRIVATE
    NOMINMAX _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
  add_dependencies(toon-viewport-material-debug-test toon-renderer-shaders)
  add_test(NAME toon-viewport-material-debug-state COMMAND toon-viewport-material-debug-test
    "${CMAKE_CURRENT_SOURCE_DIR}/material-debug.usda")
  add_test(NAME toon-viewport-material-debug-gpu COMMAND toon-viewport-material-debug-test
    "${CMAKE_CURRENT_SOURCE_DIR}/material-debug.usda"
    "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
  set_tests_properties(toon-viewport-material-debug-state toon-viewport-material-debug-gpu PROPERTIES
    TIMEOUT 60 LABELS "viewport;hydra"
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
  set_tests_properties(toon-viewport-material-debug-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu;viewport;hydra")
  add_executable(toon-viewport-skeleton-debug-test skeleton_debug_test.cpp ../src/hydra_scene.cpp)
  toon_configure_warnings(toon-viewport-skeleton-debug-test)
  target_include_directories(toon-viewport-skeleton-debug-test PRIVATE ../src)
  target_link_libraries(toon-viewport-skeleton-debug-test PRIVATE
    toon-hydra2-runtime Toon::Vulkan usdImaging usdSkel usdGeom usd)
  target_compile_definitions(toon-viewport-skeleton-debug-test PRIVATE
    NOMINMAX _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
  add_dependencies(toon-viewport-skeleton-debug-test toon-renderer-shaders)
  add_test(NAME toon-viewport-skeleton-debug-state COMMAND toon-viewport-skeleton-debug-test
    "${CMAKE_CURRENT_SOURCE_DIR}/skeleton-debug.usda")
  add_test(NAME toon-viewport-skeleton-debug-gpu COMMAND toon-viewport-skeleton-debug-test
    "${CMAKE_CURRENT_SOURCE_DIR}/skeleton-debug.usda"
    "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
  set_tests_properties(toon-viewport-skeleton-debug-state toon-viewport-skeleton-debug-gpu PROPERTIES
    TIMEOUT 60 LABELS "viewport;hydra"
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
  set_tests_properties(toon-viewport-skeleton-debug-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu;viewport;hydra")
  add_executable(toon-viewport-morph-debug-test morph_debug_test.cpp ../src/hydra_scene.cpp)
  toon_configure_warnings(toon-viewport-morph-debug-test)
  target_include_directories(toon-viewport-morph-debug-test PRIVATE ../src)
  target_link_libraries(toon-viewport-morph-debug-test PRIVATE
    toon-hydra2-runtime Toon::Vulkan usdImaging usdSkel usdGeom usd)
  target_compile_definitions(toon-viewport-morph-debug-test PRIVATE
    NOMINMAX _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
  add_dependencies(toon-viewport-morph-debug-test toon-renderer-shaders)
  add_test(NAME toon-viewport-morph-debug-state COMMAND toon-viewport-morph-debug-test
    "${CMAKE_CURRENT_SOURCE_DIR}/morph-motion.usda")
  add_test(NAME toon-viewport-morph-debug-gpu COMMAND toon-viewport-morph-debug-test
    "${CMAKE_CURRENT_SOURCE_DIR}/morph-motion.usda"
    "${CMAKE_BINARY_DIR}/backend/vulkan/shaders")
  set_tests_properties(toon-viewport-morph-debug-state toon-viewport-morph-debug-gpu PROPERTIES
    TIMEOUT 60 LABELS "viewport;hydra"
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
  set_tests_properties(toon-viewport-morph-debug-gpu PROPERTIES SKIP_RETURN_CODE 77 LABELS "gpu;viewport;hydra")
  add_test(NAME toon-viewport-playback
    COMMAND "${CMAKE_COMMAND}"
      "-DVIEWPORT=$<TARGET_FILE:toon-viewport>"
      "-DSCENE=${CMAKE_CURRENT_SOURCE_DIR}/morph-motion.usda"
      "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/playback"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/check_playback.cmake")
  add_test(NAME toon-viewport-latency
    COMMAND "${CMAKE_COMMAND}"
      "-DVIEWPORT=$<TARGET_FILE:toon-viewport>"
      "-DSCENE=${CMAKE_CURRENT_SOURCE_DIR}/morph-motion.usda"
      "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/latency"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/check_latency.cmake")
  set_tests_properties(toon-viewport-latency PROPERTIES
    SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 60
    LABELS "gpu;viewport;presentation;hydra"
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
  # With the WebGPU backend in the tree, the same fixtures on Dawn
  # (toon-viewport-webgpu-shadow-cost-*) beside the Vulkan ones.
  set(_toon_cost_backends vulkan)
  if(TOON_ENABLE_WEBGPU)
    list(APPEND _toon_cost_backends webgpu)
  endif()
  foreach(_toon_backend IN LISTS _toon_cost_backends)
    foreach(_toon_views IN ITEMS 1 4)
      set(_toon_scene shadow-cost)
      if(_toon_views EQUAL 4)
        set(_toon_scene shadow-cost-4)
      endif()
      set(_toon_name toon-viewport-shadow-cost-${_toon_views})
      set(_toon_output ${_toon_scene})
      set(_toon_labels "gpu;viewport;presentation;hydra")
      if(_toon_backend STREQUAL "webgpu")
        set(_toon_name toon-viewport-webgpu-shadow-cost-${_toon_views})
        set(_toon_output ${_toon_scene}-webgpu)
        list(APPEND _toon_labels webgpu)
      endif()
      add_test(NAME ${_toon_name}
        COMMAND "${CMAKE_COMMAND}"
          "-DVIEWPORT=$<TARGET_FILE:toon-viewport>"
          "-DBACKEND=${_toon_backend}"
          "-DSCENE=${CMAKE_CURRENT_SOURCE_DIR}/${_toon_scene}.usda"
          "-DVIEWS=${_toon_views}"
          "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${_toon_output}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/check_shadow_cost.cmake")
      set_tests_properties(${_toon_name} PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 60
        LABELS "${_toon_labels}"
        ENVIRONMENT_MODIFICATION
          "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
    endforeach()
  endforeach()
  # Vulkan/WebGPU parity of USD stages through Hydra (WebGPU policy §9):
  # each drawn on both backends at 1x and 4x over both backgrounds, the
  # scene, upload, draw, hull and shadow-view statistics equal and every
  # channel within one linear step (report 94).
  if(TOON_ENABLE_WEBGPU AND Python3_Interpreter_FOUND)
    foreach(_toon_stage IN ITEMS
        "adapters/hydra2/tests/preview-reference.usda"
        "adapters/hydra2/tests/skinning.usda"
        "adapters/hydra2/tests/geom-subsets.usda"
        "adapters/hydra2/tests/avatar-provider.usda"
        "adapters/viewport/tests/scene-lights.usda"
        "adapters/viewport/tests/shadow-cost-4.usda")
      get_filename_component(_toon_name "${_toon_stage}" NAME_WE)
      add_test(NAME toon-viewport-webgpu-usd-parity-${_toon_name}
        COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/check_webgpu_usd_parity.py"
          --viewport "$<TARGET_FILE:toon-viewport>"
          --scene "${PROJECT_SOURCE_DIR}/${_toon_stage}"
          --output "${CMAKE_CURRENT_BINARY_DIR}/webgpu-usd-parity/${_toon_name}"
          --max 1 --over-one 0)
      set_tests_properties(toon-viewport-webgpu-usd-parity-${_toon_name} PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 180
        LABELS "gpu;viewport;presentation;hydra;webgpu"
        ENVIRONMENT_MODIFICATION
          "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
    endforeach()
  endif()
  set_tests_properties(toon-viewport-playback PROPERTIES
    SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 60
    LABELS "gpu;viewport;presentation;hydra"
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
  add_test(NAME toon-viewport-playback-conflicting-time
    COMMAND toon-viewport --play --usd "${CMAKE_CURRENT_SOURCE_DIR}/morph-motion.usda"
      --time-step 1)
  set_tests_properties(toon-viewport-playback-conflicting-time PROPERTIES
    WILL_FAIL TRUE TIMEOUT 10
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
  add_test(NAME toon-viewport-morph
    COMMAND "${CMAKE_COMMAND}"
      "-DVIEWPORT=$<TARGET_FILE:toon-viewport>"
      "-DSCENE=${CMAKE_CURRENT_SOURCE_DIR}/morph-motion.usda"
      "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/morph-sequence"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/check_morph.cmake")
  set_tests_properties(toon-viewport-morph PROPERTIES
    SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 60
    LABELS "gpu;viewport;presentation;hydra"
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
  # All-frame capture must retain intermediate animation and its returning
  # image, rather than repeatedly reading a stale final frame.
  add_test(NAME toon-viewport-motion-sequence
    COMMAND toon-viewport --hidden --frames 9 --vsync off --overlay off
      --usd "${CMAKE_CURRENT_SOURCE_DIR}/outline-motion.usda"
      --time 1 --time-step 0.25 --expect-draws 1
      --capture-sequence "${CMAKE_CURRENT_BINARY_DIR}/motion-sequence"
      --camera-output "${CMAKE_CURRENT_BINARY_DIR}/motion-camera.json")
  set_tests_properties(toon-viewport-motion-sequence PROPERTIES
    PASS_REGULAR_EXPRESSION "frames read back: 9"
    LABELS "gpu;viewport;presentation;hydra" SKIP_RETURN_CODE 77 TIMEOUT 60
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
  add_test(NAME toon-viewport-motion-sequence-images
    COMMAND "${CMAKE_COMMAND}"
      "-DSEQUENCE=${CMAKE_CURRENT_BINARY_DIR}/motion-sequence"
      "-DCAMERA=${CMAKE_CURRENT_BINARY_DIR}/motion-camera.json"
      "-DREFERENCE=${CMAKE_CURRENT_BINARY_DIR}/motion-reference.ppm"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/check_sequence.cmake")
  set_tests_properties(toon-viewport-motion-sequence-images PROPERTIES
    DEPENDS "toon-viewport-motion-sequence;toon-viewport-motion-reference"
    SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "gpu;viewport;presentation;hydra")
  add_test(NAME toon-viewport-motion
    COMMAND toon-viewport --hidden --frames 9 --vsync off --overlay off
      --usd "${CMAKE_CURRENT_SOURCE_DIR}/outline-motion.usda"
      --time 1 --time-step 0.25 --expect-draws 1 --expect-hulls 0
      --screenshot "${CMAKE_CURRENT_BINARY_DIR}/motion.ppm")
  add_test(NAME toon-viewport-motion-reference
    COMMAND toon-viewport --hidden --frames 9 --vsync off --overlay off
      --usd "${CMAKE_CURRENT_SOURCE_DIR}/outline-motion.usda"
      --time 1 --expect-draws 1
      --screenshot "${CMAKE_CURRENT_BINARY_DIR}/motion-reference.ppm")
  set_tests_properties(toon-viewport-motion PROPERTIES
    FAIL_REGULAR_EXPRESSION "toon-viewport:"
    PASS_REGULAR_EXPRESSION
      "Uploads: topology=1 points=1 materials=1 textures=0 skins=1 poses=9")
  set_tests_properties(toon-viewport-motion toon-viewport-motion-reference PROPERTIES
    LABELS "gpu;viewport;presentation;hydra" SKIP_RETURN_CODE 77 TIMEOUT 60
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
  add_test(NAME toon-viewport-motion-return-image
    COMMAND "${CMAKE_COMMAND}"
      "-DFIRST=${CMAKE_CURRENT_BINARY_DIR}/motion-reference.ppm"
      "-DSECOND=${CMAKE_CURRENT_BINARY_DIR}/motion.ppm"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_captures.cmake")
  set_tests_properties(toon-viewport-motion-return-image PROPERTIES
    DEPENDS "toon-viewport-motion;toon-viewport-motion-reference"
    SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "gpu;viewport;presentation;hydra")
  # All three switches use the interactive file-open path. Compare their
  # captures to opening the final stage at startup, including overlapping
  # mesh ids/revisions, changed geometry, up axis and metres per unit.
  file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/replacement.usda"
    "${CMAKE_CURRENT_BINARY_DIR}/モデル.usda" ONLY_IF_DIFFERENT)
  set(_replacement "${CMAKE_CURRENT_BINARY_DIR}/モデル.usda")
  add_test(NAME toon-viewport-file-reference
    COMMAND toon-viewport --hidden --frames 8 --vsync off --overlay off
      --usd "${_replacement}" --expect-draws 1
      --screenshot "${CMAKE_CURRENT_BINARY_DIR}/file-reference.ppm")
  add_test(NAME toon-viewport-file-bootstrap
    COMMAND toon-viewport --hidden --frames 8 --vsync off --overlay off
      --switch-file "${_replacement}" --expect-draws 1
      --screenshot "${CMAKE_CURRENT_BINARY_DIR}/file-bootstrap.ppm")
  add_test(NAME toon-viewport-file-replace
    COMMAND toon-viewport --hidden --frames 8 --vsync off --overlay off
      --usd "${PROJECT_SOURCE_DIR}/adapters/hydra2/tests/usdview-smoke.usda"
      --switch-file "${_replacement}" --expect-draws 1
      --screenshot "${CMAKE_CURRENT_BINARY_DIR}/file-replace.ppm")
  add_test(NAME toon-viewport-file-failed
    COMMAND toon-viewport --hidden --frames 8 --vsync off --overlay off
      --usd "${_replacement}"
      --switch-file "${CMAKE_CURRENT_BINARY_DIR}/missing.usda"
      --expect-draws 1
      --screenshot "${CMAKE_CURRENT_BINARY_DIR}/file-failed.ppm")
  set_tests_properties(toon-viewport-file-failed
    PROPERTIES FAIL_REGULAR_EXPRESSION "Opened file:")
  set_tests_properties(toon-viewport-file-reference toon-viewport-file-bootstrap
    toon-viewport-file-replace toon-viewport-file-failed PROPERTIES
    LABELS "gpu;viewport;presentation;hydra"
    SKIP_RETURN_CODE 77 TIMEOUT 60
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
  foreach(_case bootstrap replace failed)
    add_test(NAME toon-viewport-file-${_case}-image
      COMMAND "${CMAKE_COMMAND}"
        "-DFIRST=${CMAKE_CURRENT_BINARY_DIR}/file-reference.ppm"
        "-DSECOND=${CMAKE_CURRENT_BINARY_DIR}/file-${_case}.ppm"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/compare_captures.cmake")
    set_tests_properties(toon-viewport-file-${_case}-image PROPERTIES
      DEPENDS "toon-viewport-file-reference;toon-viewport-file-${_case}"
      SKIP_REGULAR_EXPRESSION "SKIP:" LABELS "gpu;viewport;presentation;hydra")
  endforeach()
  if(TOON_AVATAR_RUNTIME_PROVIDERS AND TOON_VRM_IMAGING_RESOURCES)
    # The provider fixture driven by the runtime in the presenting
    # viewport. At a zero rate each frame's result is published after its
    # extraction, so every late read applies a newer one; joints, weights
    # and materials change with no static upload or Hydra sync after the
    # first frame. Results 20 and 40 follow instance resets.
    set(_avatar_stage "${PROJECT_SOURCE_DIR}/adapters/hydra2/tests/avatar-provider.usda")
    set(_avatar_run
      "-DPROGRAM=$<TARGET_FILE:toon-viewport-avatar>"
      "-DDLLS=$<JOIN:$<TARGET_RUNTIME_DLLS:toon-viewport-avatar>,|>"
      "-DPLUGINS=${TOON_VRM_SCHEMA_RESOURCES}|${TOON_VRM_IMAGING_RESOURCES}")
    add_test(NAME toon-viewport-avatar-runtime-late
      COMMAND "${CMAKE_COMMAND}" ${_avatar_run}
        "-DARGUMENTS=--hidden|--frames|48|--vsync|off|--overlay|off|--usd|${_avatar_stage}|--avatar-runtime|--avatar-lookat|/Avatar/LookAtBone|--avatar-rate|0|--avatar-reset-every|20"
        "-DEXPECT=results=49 extracted=49 late=48 held=0 refused=0 resets=2 epoch=1 generation=3|matched skins=2 morphs=6 materials=4 visibility=0 mismatches=0|static_uploads=0 poses=[1-9][0-9]* morph_weights=[1-9][0-9]* materials=[1-9]|Late latch: applied=48 rejected=0|Hydra syncs=1 |avatar publish_latch|latency expression_latch frames=48|latency look_at_present_api frames=4[78] "
        -P "${PROJECT_SOURCE_DIR}/adapters/hydra2/tests/run_avatar_providers.cmake")
    # A producer thread at 240 results per second, and the scene replaced
    # halfway: the replacement binds its own runtime in a new epoch.
    add_test(NAME toon-viewport-avatar-runtime-thread
      COMMAND "${CMAKE_COMMAND}" ${_avatar_run}
        "-DARGUMENTS=--hidden|--frames|120|--vsync|off|--overlay|off|--usd|${_avatar_stage}|--avatar-runtime|--avatar-lookat|/Avatar/LookAtExpression|--avatar-rate|240|--switch-file|${_avatar_stage}"
        "-DEXPECT=Opened file:|refused=0 resets=0 epoch=2 |mismatches=0|Late latch: applied=120 rejected=0|avatar sample_publish"
        -P "${PROJECT_SOURCE_DIR}/adapters/hydra2/tests/run_avatar_providers.cmake")
    # The runtime's motion clip provider poses the avatar first; its pose
    # samples follow the clip, one result per frame, all applied late.
    add_test(NAME toon-viewport-avatar-runtime-motion
      COMMAND "${CMAKE_COMMAND}" ${_avatar_run}
        "-DARGUMENTS=--hidden|--frames|48|--vsync|off|--overlay|off|--usd|${_avatar_stage}|--avatar-runtime|--avatar-lookat|/Avatar/LookAtBone|--avatar-rate|0|--avatar-motion|${PROJECT_SOURCE_DIR}/adapters/hydra2/tests/avatar-motion.usda"
        "-DEXPECT=results=49 extracted=49 late=48 held=0 refused=0|clip=0..1s pose_samples=49 held_poses=0|mismatches=0|static_uploads=0 poses=[1-9][0-9]*|Hydra syncs=1 "
        -P "${PROJECT_SOURCE_DIR}/adapters/hydra2/tests/run_avatar_providers.cmake")
    set_tests_properties(toon-viewport-avatar-runtime-late toon-viewport-avatar-runtime-thread
      toon-viewport-avatar-runtime-motion PROPERTIES
      LABELS "gpu;viewport;presentation;hydra"
      SKIP_REGULAR_EXPRESSION "SKIP: no Vulkan device" TIMEOUT 120
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
  endif()
  # The usdview smoke stage through Hydra: its one mesh must draw.
  add_test(NAME toon-viewport-present-usd
    COMMAND toon-viewport --hidden --frames 8 --vsync off
      --usd "${PROJECT_SOURCE_DIR}/adapters/hydra2/tests/usdview-smoke.usda"
      --expect-draws 1)
  set_tests_properties(toon-viewport-present-usd PROPERTIES
    LABELS "gpu;viewport;presentation;hydra"
    SKIP_RETURN_CODE 77
    TIMEOUT 60
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${TOON_PXR_ROOT}/bin;PATH=path_list_prepend:${TOON_PXR_ROOT}/lib")
endif()
