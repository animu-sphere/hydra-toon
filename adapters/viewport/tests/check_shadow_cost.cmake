# SPDX-License-Identifier: Apache-2.0
# A bounded fixture of the shared shadow views' cost (shadow policy §8):
# SCENE, shadow-cost.usda or a layer over it, drawn FRAMES frames at
# 256x256 with vsync off and the default samples and resolution. Its
# sixteen skinned casters and one morphed caster move by pose and weight
# alone, so after the first frame no static resource is uploaded again.
# The run must draw VIEWS views and records in OUTPUT.json the scene, the
# device, the samples, the views' resolution, layers and memory, and the
# mean, p95 and p99 of the GPU shadow pass, the GPU work, the CPU fit and
# the CPU submit. BACKEND, vulkan by default, names the viewport's backend
# (WebGPU policy §9 compares the two from one tree).
foreach(required IN ITEMS VIEWPORT SCENE VIEWS OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
if(NOT DEFINED FRAMES)
  set(FRAMES 64)
endif()
if(NOT DEFINED BACKEND)
  set(BACKEND vulkan)
endif()

execute_process(COMMAND "${VIEWPORT}" --backend ${BACKEND} --hidden --width 256 --height 256
  --frames ${FRAMES} --vsync off --overlay off --usd "${SCENE}"
  --time 1 --time-step 0.25 --expect-draws 17
  --telemetry-output "${OUTPUT}-telemetry.json"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${OUTPUT}.log" "${output}\n${error}")
if(result EQUAL 77)
  message("SKIP: ${output} ${error}")
  return()
endif()
if(NOT result EQUAL 0)
  message(FATAL_ERROR "the cost run failed: ${output} ${error}")
endif()

math(EXPR poses "16 * ${FRAMES}")
if(NOT output MATCHES "Uploads: topology=17 points=17 materials=3 textures=0 skins=16 poses=${poses} morphs=1 morph_weights=${FRAMES} ")
  message(FATAL_ERROR "a pose or weight uploaded a static resource: ${output}")
endif()
if(NOT output MATCHES "Shadow views: drawn=([0-9]+) unviewed=0 resolution=([0-9]+) layers=([0-9]+) memory_bytes=([0-9]+)")
  message(FATAL_ERROR "no shadow view statistics: ${output}")
endif()
set(views "${CMAKE_MATCH_1}")
set(resolution "${CMAKE_MATCH_2}")
set(layers "${CMAKE_MATCH_3}")
set(memory "${CMAKE_MATCH_4}")
# A D32 layer is at least four bytes a texel.
math(EXPR least "${views} * ${resolution} * ${resolution} * 4")
if(NOT views EQUAL VIEWS OR NOT resolution EQUAL 2048 OR layers LESS views OR
   memory LESS least)
  message(FATAL_ERROR "expected ${VIEWS} views at 2048 texels: ${output}")
endif()
if(NOT output MATCHES "Device: ([^\r\n]+)")
  message(FATAL_ERROR "no device: ${output}")
endif()
set(device "${CMAKE_MATCH_1}")
if(NOT output MATCHES "Presentation: [^\r\n]*, ([0-9]+) sample")
  message(FATAL_ERROR "no sample count: ${output}")
endif()
set(samples "${CMAKE_MATCH_1}")

file(READ "${OUTPUT}-telemetry.json" telemetry)
get_filename_component(scene "${SCENE}" NAME)
set(record "{}")
string(JSON record SET "${record}" scene "\"${scene}\"")
string(JSON record SET "${record}" backend "\"${BACKEND}\"")
string(JSON record SET "${record}" device "\"${device}\"")
string(JSON record SET "${record}" width 256)
string(JSON record SET "${record}" height 256)
string(JSON record SET "${record}" samples ${samples})
string(JSON record SET "${record}" vsync "\"off\"")
string(JSON record SET "${record}" frames ${FRAMES})
string(JSON record SET "${record}" shadow_views ${views})
string(JSON record SET "${record}" shadow_resolution ${resolution})
string(JSON record SET "${record}" shadow_layers ${layers})
string(JSON record SET "${record}" shadow_memory_bytes ${memory})
string(JSON record SET "${record}" ms "{}")
set(summary "")
foreach(series IN ITEMS gpu_shadow gpu_work latency_shadow_fit cpu_submit)
  string(JSON count GET "${telemetry}" summaries_ms ${series} count)
  if(count LESS 1)
    message(FATAL_ERROR "${series} has no samples: ${telemetry}")
  endif()
  string(JSON record SET "${record}" ms ${series} "{}")
  foreach(statistic IN ITEMS mean p95 p99)
    string(JSON value GET "${telemetry}" summaries_ms ${series} ${statistic})
    string(JSON record SET "${record}" ms ${series} ${statistic} ${value})
  endforeach()
  string(JSON mean GET "${record}" ms ${series} mean)
  string(JSON p95 GET "${record}" ms ${series} p95)
  string(JSON p99 GET "${record}" ms ${series} p99)
  string(APPEND summary " ${series}=${mean}/${p95}/${p99}")
endforeach()
string(JSON shadow GET "${record}" ms gpu_shadow mean)
if(shadow LESS_EQUAL 0)
  message(FATAL_ERROR "the GPU shadow pass took no time: ${telemetry}")
endif()
file(WRITE "${OUTPUT}.json" "${record}\n")
message("${scene} on ${BACKEND}, ${device}, ${samples} samples, ${views} views of ${resolution} texels, "
  "${memory} bytes; mean/p95/p99 ms:${summary}")
