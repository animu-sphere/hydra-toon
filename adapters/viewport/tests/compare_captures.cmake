# Compares two captures byte for byte. Reports SKIP, for the test's
# SKIP_REGULAR_EXPRESSION, when either is missing because the runs that
# write them skipped where no GPU can present.
foreach(capture IN ITEMS "${FIRST}" "${SECOND}")
  if(NOT EXISTS "${capture}")
    message("SKIP: ${capture} was not captured")
    return()
  endif()
endforeach()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${FIRST}" "${SECOND}"
  RESULT_VARIABLE differ)
if(NOT differ EQUAL 0)
  message(FATAL_ERROR "${FIRST} and ${SECOND} differ")
endif()
message("${FIRST} and ${SECOND} are the same image")
