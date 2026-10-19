# SPDX-License-Identifier: Apache-2.0
# The actual presentation path exports ordered monotonic endpoints, counts
# new input updates once, and retains all static uploads during animation.
execute_process(COMMAND "${VIEWPORT}" --hidden --width 96 --height 96
  --frames 16 --vsync off --overlay off --usd "${SCENE}"
  --time 1 --time-step 0.05 --expect-draws 1 --telemetry-output "${OUTPUT}.json"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
file(WRITE "${OUTPUT}.log" "${output}\n${error}")
if(result EQUAL 77)
  message("SKIP: ${output} ${error}")
  return()
endif()
if(NOT result EQUAL 0)
  message(FATAL_ERROR "latency run failed: ${output} ${error}")
endif()
if(NOT output MATCHES "Late latch: applied=16 rejected=0" OR
   NOT output MATCHES "Uploads: topology=1 points=1 materials=1 textures=0 skins=1 poses=1 morphs=1 morph_weights=16 ")
  message(FATAL_ERROR "late samples or static-upload evidence wrong: ${output}")
endif()
file(READ "${OUTPUT}.json" data)
string(JSON measured GET "${data}" display_time_measured)
string(JSON endpoint GET "${data}" present_endpoint)
string(JSON frames LENGTH "${data}" frames)
string(JSON cameras GET "${data}" summaries_ms latency_camera_present_api count)
string(JSON poses GET "${data}" summaries_ms latency_pose_submit count)
if(measured OR NOT endpoint STREQUAL "vkQueuePresentKHR_return" OR
   NOT frames EQUAL 16 OR NOT cameras EQUAL 1 OR NOT poses EQUAL 16)
  message(FATAL_ERROR "latency endpoint or input sample counts wrong: ${data}")
endif()
foreach(index RANGE 0 15)
  set(previous 0)
  foreach(field pose_input_ns latched_ns buffers_written_ns submitted_ns present_returned_ns)
    string(JSON stamp GET "${data}" frames ${index} ${field})
    if(stamp LESS_EQUAL 0 OR stamp LESS previous)
      message(FATAL_ERROR "out-of-order ${field} in frame ${index}")
    endif()
    set(previous "${stamp}")
  endforeach()
  string(JSON applied GET "${data}" frames ${index} late_sample_applied)
  if(NOT applied)
    message(FATAL_ERROR "frame ${index} omitted the late read")
  endif()
endforeach()
