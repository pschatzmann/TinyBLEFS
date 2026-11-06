# Runs a test executable with an empty scratch directory as argument.
#   cmake -DEXE=<executable> -DDIR=<scratch dir> -P RunTest.cmake
file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
execute_process(COMMAND "${EXE}" "${DIR}" RESULT_VARIABLE result)
file(REMOVE_RECURSE "${DIR}")
if(NOT result EQUAL 0)
  message(FATAL_ERROR "${EXE} failed with ${result}")
endif()
