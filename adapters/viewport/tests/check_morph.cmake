# SPDX-License-Identifier: Apache-2.0
execute_process(COMMAND "${VIEWPORT}" --hidden --frames 9 --vsync off --overlay off
  --usd "${SCENE}" --time 1 --time-step 0.25 --expect-draws 1
  --capture-sequence "${OUTPUT}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${OUTPUT}.log" "${output}\n${error}")
if(result EQUAL 77)
  message("SKIP: ${output} ${error}")
  return()
endif()
if(NOT result EQUAL 0)
  message(FATAL_ERROR "morph viewport failed: ${output} ${error}")
endif()
if(NOT output MATCHES "Uploads: topology=1 points=1 materials=1 textures=0 skins=1 poses=1 morphs=1 morph_weights=9 ")
  message(FATAL_ERROR "morph animation did not keep static GPU state resident: ${output}")
endif()
file(SHA256 "${OUTPUT}/frame-000000.ppm" first)
file(SHA256 "${OUTPUT}/frame-000004.ppm" middle)
file(SHA256 "${OUTPUT}/frame-000008.ppm" last)
if(NOT first STREQUAL last OR first STREQUAL middle)
  message(FATAL_ERROR "morph capture did not deform and return")
endif()
