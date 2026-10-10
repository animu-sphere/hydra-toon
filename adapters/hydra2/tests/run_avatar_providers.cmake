# SPDX-License-Identifier: Apache-2.0
#
# Runs toon-hydra2-avatar-provider-test with what its providers need at run
# time: the directories of the DLLs it links (the runtime, the VRM schema and
# the motion owners) on PATH, and the VRM schema and imaging plugins'
# resources registered. Arguments after the stage are separated by `|`.
foreach(required IN ITEMS PROGRAM STAGE DLLS PLUGINS)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

string(REPLACE "|" ";" dlls "${DLLS}")
set(directories "")
foreach(dll IN LISTS dlls)
  get_filename_component(directory "${dll}" DIRECTORY)
  list(APPEND directories "${directory}")
endforeach()
list(REMOVE_DUPLICATES directories)
cmake_path(CONVERT "${directories};$ENV{PATH}" TO_NATIVE_PATH_LIST runtime_path NORMALIZE)
string(REPLACE "|" ";" plugins "${PLUGINS}")
cmake_path(CONVERT "${plugins}" TO_NATIVE_PATH_LIST plugin_path NORMALIZE)
string(REPLACE "|" ";" arguments "${ARGUMENTS}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "PATH=${runtime_path}"
    "PXR_PLUGINPATH_NAME=${plugin_path}"
    "${PROGRAM}" "${STAGE}" ${arguments}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
message("${output}${error}")
if(result EQUAL 77)
  message("SKIP: no Vulkan device for the image comparison")
elseif(NOT result EQUAL 0)
  message(FATAL_ERROR "the provider host check failed (${result})")
endif()
