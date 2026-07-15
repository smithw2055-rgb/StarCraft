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
set(_stdout_log "${STARCRAFT_SMOKE_LOG_DIR}/StarCraftInteractionSmoke.stdout.log")
set(_stderr_log "${STARCRAFT_SMOKE_LOG_DIR}/StarCraftInteractionSmoke.stderr.log")
set(_test_map "${STARCRAFT_WORKDIR}/assets/locales/zhCN/Assets/campaign/EXPTerran/Terran02/staredit/scenario.chk")
if(NOT EXISTS "${_test_map}")
    message(FATAL_ERROR "StarCraft interaction smoke test map is missing: ${_test_map}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        STARCRAFT_DEV_START_INGAME=1
        STARCRAFT_INTERACTION_SMOKE=1
        STARCRAFT_FULL_ROSTER_SMOKE=1
        STARCRAFT_LOG_FPS=1
        "${STARCRAFT_EXE}" --test-map "${_test_map}" --exit-after-frame 20
    WORKING_DIRECTORY "${STARCRAFT_WORKDIR}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 45)

file(WRITE "${_stdout_log}" "${_stdout}")
file(WRITE "${_stderr_log}" "${_stderr}")

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "StarCraft interaction smoke failed with exit code ${_result}. Logs: ${_stdout_log}; ${_stderr_log}")
endif()

foreach(_required IN ITEMS
    "StarCraft: Game started"
    "Loaded HD tileset atlas"
    "Opened direct CHK:"
    "120 units, tileset=platform"
    "triggers=89"
    "full roster spawned buildings="
    "Interaction smoke passed:")
    if(NOT _stdout MATCHES "${_required}")
        message(FATAL_ERROR "StarCraft interaction smoke missing expected log '${_required}'. Logs: ${_stdout_log}; ${_stderr_log}")
    endif()
endforeach()

if(_stdout MATCHES "\\[ERROR\\]|ResourceCache init failed|Missing sprite|Interaction smoke failed")
    message(FATAL_ERROR "StarCraft interaction smoke found a non-WebM runtime failure. Logs: ${_stdout_log}; ${_stderr_log}")
endif()

message(STATUS "StarCraft interaction smoke passed. Logs: ${_stdout_log}; ${_stderr_log}")
