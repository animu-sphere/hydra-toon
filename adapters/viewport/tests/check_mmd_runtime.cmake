cmake_minimum_required(VERSION 3.24)
string(REPLACE "|" ";" dlls "${DLLS}")
foreach(dll IN LISTS dlls)
  get_filename_component(directory "${dll}" DIRECTORY)
  cmake_path(CONVERT "${directory}" TO_NATIVE_PATH_LIST directory)
  if(WIN32)
    set(ENV{PATH} "${directory};$ENV{PATH}")
  endif()
endforeach()
string(REPLACE "|" ";" plugins "${PLUGINS}")
set(ENV{PXR_PLUGINPATH_NAME} "${plugins}")
file(TO_CMAKE_PATH "${PMX}" source_pmx)
set(composed "${OUTPUT}.usda")
file(WRITE "${composed}" "#usda 1.0\n(\n defaultPrim = \"Asset\"\n upAxis = \"Y\"\n metersPerUnit = 1\n kilogramsPerUnit = 1\n subLayers = [@${source_pmx}@]\n)\n")
execute_process(COMMAND "${VIEWPORT}" --usd "${composed}" --mmd-physics --avatar-model "${PMX}" --avatar-motion "${VMD}"
  --play --hidden --frames 90 --width 320 --height 320 --samples 4 --vsync on
  --avatar-reset-every 20 --switch-file "${composed}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 90)
file(WRITE "${OUTPUT}" "${output}\n${errors}")
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Live MMD viewport failed (${result}): ${output}\n${errors}")
endif()
foreach(pattern IN ITEMS
    "Opened file:"
    "Animation: Hydra syncs=1 "
    "MMD physics: steps=[1-9][0-9]* frame=[1-9][0-9]*.*refused=0 resets=[1-9][0-9]* skins=1 morphs=1 uv_meshes=1 mismatches=0"
    "MMD physics: closed bodies=0"
    "Presented 90 frames")
  if(NOT output MATCHES "${pattern}")
    message(FATAL_ERROR "Missing live physics evidence ${pattern}: ${output}\n${errors}")
  endif()
endforeach()
if(errors MATCHES "MMD physics paused:|MMD physics control:|MMD physics close:")
  message(FATAL_ERROR "Live physics refused: ${errors}")
endif()
get_filename_component(vmd_directory "${VMD}" DIRECTORY)
set(unsupported "${vmd_directory}/live-physics-material.vmd")
if(EXISTS "${unsupported}")
  execute_process(COMMAND "${VIEWPORT}" --usd "${composed}" --mmd-physics --avatar-model "${PMX}"
    --avatar-motion "${unsupported}" --play --hidden --frames 16 --width 320 --height 320 --vsync on
    RESULT_VARIABLE refusal OUTPUT_VARIABLE refused_output ERROR_VARIABLE refused_errors TIMEOUT 30)
  if(NOT refusal EQUAL 1 OR NOT refused_errors MATCHES "bounded MMD run contains refused physics evaluations")
    message(FATAL_ERROR "Refused simulation was reported as success: ${refusal}\n${refused_output}\n${refused_errors}")
  endif()
endif()
