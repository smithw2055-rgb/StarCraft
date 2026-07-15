if(NOT DEFINED STARCRAFT_EXE)
    message(FATAL_ERROR "STARCRAFT_EXE is required")
endif()

if(NOT DEFINED STARCRAFT_WORKDIR)
    message(FATAL_ERROR "STARCRAFT_WORKDIR is required")
endif()

if(NOT DEFINED STARCRAFT_SMOKE_LOG_DIR)
    set(STARCRAFT_SMOKE_LOG_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()

file(MAKE_DIRECTORY "${STARCRAFT_SMOKE_LOG_DIR}")
set(_stdout_log "${STARCRAFT_SMOKE_LOG_DIR}/StarCraftRuntimeSmoke.stdout.log")
set(_stderr_log "${STARCRAFT_SMOKE_LOG_DIR}/StarCraftRuntimeSmoke.stderr.log")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        STARCRAFT_DEV_START_INGAME=1
        STARCRAFT_LOG_FPS=1
        "${STARCRAFT_EXE}" --exit-after-frame 120
    WORKING_DIRECTORY "${STARCRAFT_WORKDIR}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 45)

file(WRITE "${_stdout_log}" "${_stdout}")
file(WRITE "${_stderr_log}" "${_stderr}")

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "StarCraft runtime smoke failed with exit code ${_result}. Logs: ${_stdout_log}; ${_stderr_log}")
endif()

foreach(_required IN ITEMS
    "StarCraft: Game started"
    "Loaded HD tileset atlas"
    "Terran HUD scene panel"
    "Loaded CHK triggers"
    "Campaign scripting initialized"
    "fps=")
    if(NOT _stdout MATCHES "${_required}")
        message(FATAL_ERROR "StarCraft runtime smoke missing expected log '${_required}'. Logs: ${_stdout_log}; ${_stderr_log}")
    endif()
endforeach()

if(_stdout MATCHES "\\[ERROR\\]|ResourceCache init failed|Missing sprite")
    message(FATAL_ERROR "StarCraft runtime smoke found a non-WebM startup failure. Logs: ${_stdout_log}; ${_stderr_log}")
endif()

message(STATUS "StarCraft runtime smoke passed. Logs: ${_stdout_log}; ${_stderr_log}")
