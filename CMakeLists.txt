cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(StarcraftRemake VERSION 0.1.0 LANGUAGES CXX)

enable_testing()

if(NOT CMAKE_TOOLCHAIN_FILE)
    # Priority: explicit VCPKG_ROOT env > %USERPROFILE%\vcpkg > sibling dir
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
    string(STRIP "${VCPKG_ROOT}" VCPKG_ROOT)
    if(NOT VCPKG_ROOT)
        if(WIN32 AND DEFINED ENV{USERPROFILE})
            set(_VCPKG_USER "$ENV{USERPROFILE}/vcpkg")
            if(EXISTS "${_VCPKG_USER}/scripts/buildsystems/vcpkg.cmake")
                set(VCPKG_ROOT "${_VCPKG_USER}")
            endif()
        endif()
    endif()
    if(NOT VCPKG_ROOT AND EXISTS "${CMAKE_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake")
        get_filename_component(VCPKG_ROOT "${CMAKE_SOURCE_DIR}/../vcpkg" ABSOLUTE)
    endif()
    if(VCPKG_ROOT)
        set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "vcpkg toolchain")
        message(STATUS "Using vcpkg from: ${VCPKG_ROOT}")
    else()
        message(WARNING "vcpkg not configured. Set VCPKG_ROOT or install vcpkg.")
    endif()
endif()

if(DEFINED CMAKE_TOOLCHAIN_FILE)
    set(VCPKG_MANIFEST_MODE ON)
    set(VCPKG_MANIFEST_DIR "${CMAKE_SOURCE_DIR}")
    set(VCPKG_MANIFEST_INSTALL ON)
endif()

set(_ACTIVE_VCPKG_ROOT "${VCPKG_ROOT}")
string(STRIP "${_ACTIVE_VCPKG_ROOT}" _ACTIVE_VCPKG_ROOT)
set(_VCPKG_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}")
string(STRIP "${_VCPKG_TOOLCHAIN_FILE}" _VCPKG_TOOLCHAIN_FILE)
if(NOT _ACTIVE_VCPKG_ROOT AND DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${_VCPKG_TOOLCHAIN_FILE}")
    get_filename_component(_VCPKG_BUILD_DIR "${_VCPKG_TOOLCHAIN_FILE}" DIRECTORY)
    get_filename_component(_VCPKG_SCRIPTS_DIR "${_VCPKG_BUILD_DIR}" DIRECTORY)
    get_filename_component(_ACTIVE_VCPKG_ROOT "${_VCPKG_SCRIPTS_DIR}" DIRECTORY)
endif()

set(_VCPKG_INSTALLED_HINT "")
if(_ACTIVE_VCPKG_ROOT AND EXISTS "${_ACTIVE_VCPKG_ROOT}/installed/x64-windows")
    set(_VCPKG_INSTALLED_HINT "${_ACTIVE_VCPKG_ROOT}/installed/x64-windows")
    list(APPEND CMAKE_PREFIX_PATH "${_VCPKG_INSTALLED_HINT}")

    if(EXISTS "${_VCPKG_INSTALLED_HINT}/share/stormlib/StormLibConfig.cmake")
        set(StormLib_DIR "${_VCPKG_INSTALLED_HINT}/share/stormlib" CACHE PATH "StormLib package config" FORCE)
    elseif(EXISTS "${_VCPKG_INSTALLED_HINT}/share/StormLib/StormLibConfig.cmake")
        set(StormLib_DIR "${_VCPKG_INSTALLED_HINT}/share/StormLib" CACHE PATH "StormLib package config" FORCE)
    endif()

    if(EXISTS "${_VCPKG_INSTALLED_HINT}/share/casclib/CascLibConfig.cmake")
        set(CascLib_DIR "${_VCPKG_INSTALLED_HINT}/share/casclib" CACHE PATH "CascLib package config" FORCE)
    endif()
endif()


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


if(WIN32)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set(CMAKE_GENERATOR_PLATFORM "x64" CACHE STRING "Platform" FORCE)
endif()


# raylib has been fully removed from this project.
set(HAS_RAYLIB FALSE)
set(raylib_TARGET "")

# GUI (optional UI integration)
option(ENABLE_GUI_UI "Enable GUI integration for the game UI" ON)
set(GUI_BASE "${CMAKE_SOURCE_DIR}/third_party/AeroGUI")
set(GUI_INCLUDE_DIR "${GUI_BASE}/Include")
set(GUI_PACKAGE_APP_DIR "${GUI_BASE}/Packages/App")
set(GUI_PACKAGE_RENDER_DIR "${GUI_BASE}/Packages/Render")
set(GUI_BIN_DIR "${GUI_BASE}/Bin/x64-release-shared" CACHE PATH "GUI DLL/import-library directory")
set(GUI_APP_LIB "${GUI_BIN_DIR}/AeroApp.lib")
set(GUI_CORE_LIB "${GUI_BIN_DIR}/Aero.lib")
set(GUI_APP_DLL "${GUI_BIN_DIR}/AeroApp.dll")
set(GUI_CORE_DLL "${GUI_BIN_DIR}/Aero.dll")

# AngelScript third-party scripting retired in Phase 51/55
set(ANGELSCRIPT_TARGET "")
set(HAS_ANGELSCRIPT FALSE)

if(EXISTS "${GUI_INCLUDE_DIR}"
    AND EXISTS "${GUI_APP_LIB}"
    AND EXISTS "${GUI_CORE_LIB}"
    AND EXISTS "${GUI_APP_DLL}"
    AND EXISTS "${GUI_CORE_DLL}")
    set(HAS_GUI TRUE)
else()
    set(HAS_GUI FALSE)
endif()

# ZLIB / BZip2: not used directly by our code (StormLib/CascLib link them
# internally as their own static deps). Probe but don't require.
find_package(ZLIB QUIET)
set(HAS_ZLIB FALSE)
if(TARGET ZLIB::ZLIB)
    set(HAS_ZLIB TRUE)
    message(STATUS "  zlib:      FOUND (config) ZLIB::ZLIB")
elseif(ZLIB_FOUND)
    set(HAS_ZLIB TRUE)
    message(STATUS "  zlib:      FOUND (legacy ${ZLIB_LIBRARIES})")
else()
    message(STATUS "  zlib:      NOT FOUND (only needed transitively by StormLib/CascLib)")
endif()

find_package(BZip2 QUIET)
if(NOT TARGET BZip2::BZip2)
    find_path(BZIP2_INCLUDE_DIR bzlib.h
        PATH_SUFFIXES include
        PATHS ${_VCPKG_INSTALLED_HINT})
    find_library(BZIP2_LIBRARY_RELEASE
        NAMES bz2
        PATHS ${_VCPKG_INSTALLED_HINT}/lib)
    find_library(BZIP2_LIBRARY_DEBUG
        NAMES bz2d
        PATHS ${_VCPKG_INSTALLED_HINT}/debug/lib)
    if(BZIP2_INCLUDE_DIR AND (BZIP2_LIBRARY_RELEASE OR BZIP2_LIBRARY_DEBUG))
        add_library(BZip2::BZip2 UNKNOWN IMPORTED)
        set_target_properties(BZip2::BZip2 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${BZIP2_INCLUDE_DIR}")
        if(BZIP2_LIBRARY_RELEASE)
            set_property(TARGET BZip2::BZip2 PROPERTY IMPORTED_LOCATION_RELEASE "${BZIP2_LIBRARY_RELEASE}")
            set_property(TARGET BZip2::BZip2 PROPERTY IMPORTED_LOCATION_RELWITHDEBINFO "${BZIP2_LIBRARY_RELEASE}")
            set_property(TARGET BZip2::BZip2 PROPERTY IMPORTED_LOCATION_MINSIZEREL "${BZIP2_LIBRARY_RELEASE}")
        endif()
        if(BZIP2_LIBRARY_DEBUG)
            set_property(TARGET BZip2::BZip2 PROPERTY IMPORTED_LOCATION_DEBUG "${BZIP2_LIBRARY_DEBUG}")
        elseif(BZIP2_LIBRARY_RELEASE)
            set_property(TARGET BZip2::BZip2 PROPERTY IMPORTED_LOCATION_DEBUG "${BZIP2_LIBRARY_RELEASE}")
        endif()
        set(BZIP2_FOUND TRUE)
    endif()
endif()
set(HAS_BZIP2 FALSE)
if(TARGET BZip2::BZip2)
    set(HAS_BZIP2 TRUE)
    message(STATUS "  bzip2:     FOUND (config) BZip2::BZip2")
elseif(BZIP2_FOUND)
    set(HAS_BZIP2 TRUE)
    message(STATUS "  bzip2:     FOUND (legacy ${BZIP2_LIBRARIES})")
else()
    message(STATUS "  bzip2:     NOT FOUND (only needed transitively by StormLib/CascLib)")
endif()
find_package(StormLib CONFIG QUIET)
if(TARGET StormLib::storm)
    set(StormLib_TARGET StormLib::storm)
    set(HAS_STORMLIB TRUE)
    message(STATUS "  StormLib:  FOUND (config)")
elseif(TARGET StormLib::StormLib)
    set(StormLib_TARGET StormLib::StormLib)
    set(HAS_STORMLIB TRUE)
    message(STATUS "  StormLib:  FOUND (config)")
else()
    # Try pkg-config / direct link fallback
    find_path(STORMLIB_INCLUDE_DIR StormLib.h)
    find_library(STORMLIB_LIBRARY storm)
    if(STORMLIB_INCLUDE_DIR AND STORMLIB_LIBRARY)
        add_library(StormLib UNKNOWN IMPORTED)
        set_target_properties(StormLib PROPERTIES
            IMPORTED_LOCATION "${STORMLIB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${STORMLIB_INCLUDE_DIR}")
        set(StormLib_TARGET StormLib)
        set(HAS_STORMLIB TRUE)
        message(STATUS "  StormLib:  FOUND (pkg-config/direct) at ${STORMLIB_LIBRARY}")
    else()
        add_library(StormLib INTERFACE)
        set(StormLib_TARGET StormLib)
        message(STATUS "  StormLib:  NOT FOUND")
    endif()
endif()

find_package(CascLib CONFIG QUIET)
if(TARGET CascLib::casc)
    set(CascLib_TARGET CascLib::casc)
    set(HAS_CASCLIB TRUE)
    message(STATUS "  CascLib:   FOUND (config)")
elseif(TARGET CascLib::CascLib)
    set(CascLib_TARGET CascLib::CascLib)
    set(HAS_CASCLIB TRUE)
    message(STATUS "  CascLib:   FOUND (config)")
else()
    # Try pkg-config / direct link fallback
    find_path(CASCLIB_INCLUDE_DIR CascLib.h)
    find_library(CASCLIB_LIBRARY casc)
    if(CASCLIB_INCLUDE_DIR AND CASCLIB_LIBRARY)
        add_library(CascLib UNKNOWN IMPORTED)
        set_target_properties(CascLib PROPERTIES
            IMPORTED_LOCATION "${CASCLIB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${CASCLIB_INCLUDE_DIR}")
        set(CascLib_TARGET CascLib)
        set(HAS_CASCLIB TRUE)
        message(STATUS "  CascLib:   FOUND (pkg-config/direct) at ${CASCLIB_LIBRARY}")
    else()
        add_library(CascLib INTERFACE)
        set(CascLib_TARGET CascLib)
        message(STATUS "  CascLib:   NOT FOUND")
    endif()
endif()

if(NOT unofficial-libvpx_DIR AND DEFINED ENV{USERPROFILE})
    set(_USER_LIBVPX_DIR "$ENV{USERPROFILE}/vcpkg/installed/x64-windows/share/unofficial-libvpx")
    if(EXISTS "${_USER_LIBVPX_DIR}/unofficial-libvpx-config.cmake")
        set(unofficial-libvpx_DIR "${_USER_LIBVPX_DIR}" CACHE PATH "libvpx package config" FORCE)
    endif()
endif()
find_package(unofficial-libvpx CONFIG QUIET)
if(TARGET unofficial::libvpx::libvpx)
    set(HAS_LIBVPX TRUE)
    set(LIBVPX_TARGET unofficial::libvpx::libvpx)
    message(STATUS "  libvpx:    FOUND (config)")
else()
    set(HAS_LIBVPX FALSE)
    set(LIBVPX_TARGET "")
    message(STATUS "  libvpx:    NOT FOUND (WebM decode disabled)")
endif()

# ============================================================================
# Shared Engine Core Object Library & Common Target Configuration Helper
# ============================================================================
add_library(StarCraftCoreObj OBJECT
    src/Core/ECS/EntityManager.cpp
    src/Core/ECS/SystemManager.cpp
    src/Core/Utils/Logger.cpp
)
target_include_directories(StarCraftCoreObj PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
if(MSVC)
    target_compile_options(StarCraftCoreObj PRIVATE /W4 /utf-8 /permissive-)
    target_compile_definitions(StarCraftCoreObj PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(StarCraftCoreObj PRIVATE -Wall -Wextra -Wpedantic -pthread)
endif()

function(starcraft_configure_test_target TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(${TARGET_NAME} PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Phase 51: Legacy AngelScript exporter retired in favor of declarative AnimationServer2D clips

option(BUILD_COMBAT_RULES_TEST "Build combat rule regression test" ON)
if(BUILD_COMBAT_RULES_TEST)
    add_executable(CombatRulesTest
        tests/Regression/CombatRulesTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/IScriptVM.cpp
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/BulletSystem.cpp
        src/Game/Systems/CombatSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
    )

    starcraft_configure_test_target(CombatRulesTest)
endif()

option(BUILD_SPELL_VISUAL_STATE_TEST "Build spell visual state regression test" ON)
if(BUILD_SPELL_VISUAL_STATE_TEST)
    add_executable(SpellVisualStateTest
        tests/Regression/SpellVisualStateTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/SpellSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
    )

    starcraft_configure_test_target(SpellVisualStateTest)
endif()

option(BUILD_ANIMATION_STATE_TEST "Build animation state regression test" ON)
if(BUILD_ANIMATION_STATE_TEST)
    add_executable(AnimationStateTest
        tests/Regression/AnimationStateTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
    )

    starcraft_configure_test_target(AnimationStateTest)
endif()

option(BUILD_ANIMATION_RUNTIME_DIRECTION_TEST "Build animation runtime direction regression test" ON)
if(BUILD_ANIMATION_RUNTIME_DIRECTION_TEST)
    add_executable(AnimationRuntimeDirectionTest
        tests/Regression/AnimationRuntimeDirectionTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/IScriptVM.cpp
        src/Game/Systems/AnimationSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Races/RaceDef.cpp
    )

    starcraft_configure_test_target(AnimationRuntimeDirectionTest)
endif()

option(BUILD_ISCRIPT_OVERLAY_TEST "Build iscript overlay regression test" ON)
if(BUILD_ISCRIPT_OVERLAY_TEST)
    add_executable(IScriptOverlayTest
        tests/Regression/IScriptOverlayTest.cpp
        src/Core/Utils/Logger.cpp
        src/Game/IScriptVM.cpp
    )

    starcraft_configure_test_target(IScriptOverlayTest)
endif()

option(BUILD_SPRITE_IMAGE_DATA_TEST "Build sprite/image data mapping regression test" ON)
if(BUILD_SPRITE_IMAGE_DATA_TEST)
    add_executable(SpriteImageDataTest
        tests/Regression/SpriteImageDataTest.cpp
        src/Core/Utils/Logger.cpp
        src/Game/IScriptVM.cpp
        src/Resource/CascArchive.cpp
        src/Resource/DatDecoder.cpp
        src/Resource/FileSystemArchive.cpp
        src/Resource/GameDataLoader.cpp
        src/Resource/GrpDecoder.cpp
        src/Resource/IArchiveReader.cpp
        src/Resource/MpqArchive.cpp
        src/Resource/Palette.cpp
        src/Resource/TblDecoder.cpp
    )

    starcraft_configure_test_target(SpriteImageDataTest)
endif()

option(BUILD_RACE_DEFINITION_MAPPING_TEST "Build race definition mapping regression test" ON)
if(BUILD_RACE_DEFINITION_MAPPING_TEST)
    add_executable(RaceDefinitionMappingTest
        tests/Regression/RaceDefinitionMappingTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Races/RaceDef.cpp
    )

    starcraft_configure_test_target(RaceDefinitionMappingTest)
endif()

option(BUILD_AI_BRAIN_STRATEGY_TEST "Build AI difficulty strategy regression test" ON)
if(BUILD_AI_BRAIN_STRATEGY_TEST)
    add_executable(AIBrainStrategyTest
        tests/Regression/AIBrainStrategyTest.cpp
    )

    starcraft_configure_test_target(AIBrainStrategyTest)
endif()

option(BUILD_COMMAND_SYSTEM_ORDER_TEST "Build command system order legality regression test" ON)
if(BUILD_COMMAND_SYSTEM_ORDER_TEST)
    add_executable(CommandSystemOrderTest
        tests/Regression/CommandSystemOrderTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/CommandSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Main/GameLoop/InputHandler.cpp
    )

    starcraft_configure_test_target(CommandSystemOrderTest)
endif()

option(BUILD_HUD_MINIMAP_GEOMETRY_TEST "Build HUD minimap geometry regression test" ON)
if(BUILD_HUD_MINIMAP_GEOMETRY_TEST)
    add_executable(HudMinimapGeometryTest
        tests/Regression/HudMinimapGeometryTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
    )

    starcraft_configure_test_target(HudMinimapGeometryTest)
endif()

option(BUILD_FOG_OF_WAR_STATUS_TEST "Build fog of war status regression test" ON)
if(BUILD_FOG_OF_WAR_STATUS_TEST)
    add_executable(FogOfWarStatusTest
        tests/Regression/FogOfWarStatusTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/Map.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/FogOfWarSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
    )

    starcraft_configure_test_target(FogOfWarStatusTest)
endif()

option(BUILD_HUD_COMMAND_METADATA_TEST "Build HUD command metadata regression test" ON)
if(BUILD_HUD_COMMAND_METADATA_TEST)
    add_executable(HudCommandMetadataTest
        tests/Regression/HudCommandMetadataTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/BuildingSystem.cpp
        src/Game/Systems/CommandSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/ProductionSystem.cpp
        src/Game/Systems/ProtossSystem.cpp
        src/Game/Systems/SpellSystem.cpp
        src/Game/Systems/NukeAndSpiderMineSystem.cpp
        src/Game/Systems/TransportSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Systems/ZergSystem.cpp
        src/Main/GameLoop/CursorTypes.cpp
        src/Main/GameLoop/InputHandler.cpp
        src/UI/HudCommandPanel.cpp
        src/UI/HudContext.cpp
        src/UI/HudInfoPanelState.cpp
    )

    starcraft_configure_test_target(HudCommandMetadataTest)
endif()

option(BUILD_CHK_STRING_ENCODING_TEST "Build CHK string encoding regression test" ON)
if(BUILD_CHK_STRING_ENCODING_TEST)
    add_executable(ChkStringEncodingTest
        tests/Regression/ChkStringEncodingTest.cpp
        src/Game/Map/ChkParser.cpp
    )

    starcraft_configure_test_target(ChkStringEncodingTest)
endif()

option(BUILD_SMART_CLICK_COMMAND_TEST "Build smart-click command dispatch regression test" ON)
if(BUILD_SMART_CLICK_COMMAND_TEST)
    add_executable(SmartClickCommandTest
        tests/Regression/SmartClickCommandTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/BuildingSystem.cpp
        src/Game/Systems/CommandSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/ProductionSystem.cpp
        src/Game/Systems/ProtossSystem.cpp
        src/Game/Systems/SpellSystem.cpp
        src/Game/Systems/TransportSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Systems/ZergSystem.cpp
        src/Main/GameLoop/CursorTypes.cpp
        src/Main/GameLoop/InputHandler.cpp
        src/UI/HudCommandPanel.cpp
        src/UI/HudContext.cpp
        src/UI/HudInfoPanelState.cpp
    )

    starcraft_configure_test_target(SmartClickCommandTest)
endif()

option(BUILD_SELECTION_INTERACTION_TEST "Build selection interaction regression test" ON)
if(BUILD_SELECTION_INTERACTION_TEST)
    add_executable(SelectionInteractionTest
        tests/Regression/SelectionInteractionTest.cpp
        src/Client/Selection.cpp
        src/Core/ECS/EntityManager.cpp
    )

    starcraft_configure_test_target(SelectionInteractionTest)
endif()

option(BUILD_SELECTION_FOOTPRINT_TEST "Build selection footprint regression test" ON)
if(BUILD_SELECTION_FOOTPRINT_TEST)
    add_executable(SelectionFootprintTest
        tests/Regression/SelectionFootprintTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
    )

    starcraft_configure_test_target(SelectionFootprintTest)
endif()

option(BUILD_MOUSE_COORDINATE_TEST "Build mouse coordinate regression test" ON)
if(BUILD_MOUSE_COORDINATE_TEST)
    add_executable(MouseCoordinateTest
        tests/Regression/MouseCoordinateTest.cpp
    )

    starcraft_configure_test_target(MouseCoordinateTest)
endif()

option(BUILD_CAMERA_EDGE_SCROLL_TEST "Build camera edge-scroll regression test" ON)
if(BUILD_CAMERA_EDGE_SCROLL_TEST)
    add_executable(CameraEdgeScrollTest
        tests/Regression/CameraEdgeScrollTest.cpp
        src/Client/CameraController.cpp
    )

    starcraft_configure_test_target(CameraEdgeScrollTest)
endif()

option(BUILD_MOVEMENT_COMMAND_TEST "Build movement command regression test" ON)
if(BUILD_MOVEMENT_COMMAND_TEST)
    add_executable(MovementCommandTest
        tests/Regression/MovementCommandTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
    )

    starcraft_configure_test_target(MovementCommandTest)
endif()

option(BUILD_NAVIGATION_PATHFINDING_TEST
    "Build minitile navigation pathfinding regression test" ON)
if(BUILD_NAVIGATION_PATHFINDING_TEST)
    add_executable(NavigationPathfindingTest
        tests/Regression/NavigationPathfindingTest.cpp)

    starcraft_configure_test_target(NavigationPathfindingTest)
endif()

option(BUILD_NAVIGATION_DETERMINISM_TEST
    "Build fixed-tick navigation determinism regression test" ON)
if(BUILD_NAVIGATION_DETERMINISM_TEST)
    add_executable(NavigationDeterminismTest
        tests/Regression/NavigationDeterminismTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp)

    starcraft_configure_test_target(NavigationDeterminismTest)
endif()

option(BUILD_NAVIGATION_STRESS_TEST
    "Build 800-unit navigation stress benchmark" ON)
if(BUILD_NAVIGATION_STRESS_TEST)
    add_executable(NavigationStressTest
        tests/Regression/NavigationStressTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp)

    starcraft_configure_test_target(NavigationStressTest)
endif()

option(BUILD_PRODUCTION_ECONOMY_TEST "Build production economy regression test" ON)
if(BUILD_PRODUCTION_ECONOMY_TEST)
    add_executable(ProductionEconomyTest
        tests/Regression/ProductionEconomyTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/BuildingSystem.cpp
        src/Game/Systems/CommandSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/ProtossSystem.cpp
        src/Game/Systems/ProductionSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Systems/ZergSystem.cpp
    )

    starcraft_configure_test_target(ProductionEconomyTest)
endif()

option(BUILD_ZERG_CREEP_REGRESSION_TEST "Build Zerg creep regression test" ON)
if(BUILD_ZERG_CREEP_REGRESSION_TEST)
    add_executable(ZergCreepRegressionTest
        tests/Regression/ZergCreepRegressionTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/BuildingSystem.cpp
        src/Game/Systems/CommandSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/ProtossSystem.cpp
        src/Game/Systems/ProductionSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Systems/ZergSystem.cpp
    )

    starcraft_configure_test_target(ZergCreepRegressionTest)
endif()

option(BUILD_CURSOR_TARGETING_TEST "Build cursor targeting regression test" ON)
if(BUILD_CURSOR_TARGETING_TEST)
    add_executable(CursorTargetingTest
        tests/Regression/CursorTargetingTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Game/Races/RaceDef.cpp
        src/Main/GameLoop/CursorTypes.cpp
    )

    starcraft_configure_test_target(CursorTargetingTest)
endif()

option(BUILD_HARVEST_SYSTEM_COMMAND_TEST "Build harvest command regression test" ON)
if(BUILD_HARVEST_SYSTEM_COMMAND_TEST)
    add_executable(HarvestSystemCommandTest
        tests/Regression/HarvestSystemCommandTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Races/RaceDef.cpp
    )

    starcraft_configure_test_target(HarvestSystemCommandTest)
endif()

option(BUILD_BUILDING_PLACEMENT_TEST "Build building placement regression test" ON)
if(BUILD_BUILDING_PLACEMENT_TEST)
    add_executable(BuildingPlacementTest
        tests/Regression/BuildingPlacementTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/BuildingSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
    )

    starcraft_configure_test_target(BuildingPlacementTest)
endif()

option(BUILD_MATCH_OUTCOME_TEST "Build match outcome regression test" ON)
if(BUILD_MATCH_OUTCOME_TEST)
    add_executable(MatchOutcomeTest
        tests/Regression/MatchOutcomeTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/FogOfWarSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/IScriptVM.cpp
        src/Game/Map/Map.cpp
        src/Game/Map/ChkParser.cpp
    )

    starcraft_configure_test_target(MatchOutcomeTest)
endif()

option(BUILD_OGG_VORBIS_DECODE_TEST "Build Ogg/Vorbis decode regression test" ON)
if(BUILD_OGG_VORBIS_DECODE_TEST)
    add_executable(OggVorbisDecodeTest
        tests/Regression/OggVorbisDecodeTest.cpp
    )

    starcraft_configure_test_target(OggVorbisDecodeTest)
endif()

option(BUILD_TRANSPORT_SYSTEM_TEST "Build transport system regression test" ON)
if(BUILD_TRANSPORT_SYSTEM_TEST)
    add_executable(TransportSystemTest
        tests/Regression/TransportSystemTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/IScriptVM.cpp
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/AnimationSystem.cpp
        src/Game/Systems/BuildingSystem.cpp
        src/Game/Systems/BulletSystem.cpp
        src/Game/Systems/CombatSystem.cpp
        src/Game/Systems/CommandSystem.cpp
        src/Game/Systems/FogOfWarSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/ProtossSystem.cpp
        src/Game/Systems/ProductionSystem.cpp
        src/Game/Systems/SpellSystem.cpp
        src/Game/Systems/NukeAndSpiderMineSystem.cpp
        src/Game/Systems/TransportSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Systems/ZergSystem.cpp
    )

    starcraft_configure_test_target(TransportSystemTest)
endif()

option(BUILD_CAMPAIGN_CATALOG_RUNTIME_TEST "Build all-campaign catalog runtime regression test" ON)
if(BUILD_CAMPAIGN_CATALOG_RUNTIME_TEST)
    add_executable(CampaignCatalogRuntimeTest
        tests/Regression/CampaignCatalogRuntimeTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Campaign/CampaignController.cpp
        src/Game/Campaign/CampaignMissionCatalog.cpp
        src/Game/Trigger/TriggerVM.cpp
        src/Game/AI/BehaviorTreeRuntime.cpp
        src/Game/IScriptVM.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/AnimationSystem.cpp
        src/Game/Systems/CommandSystem.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Client/MapLoader.cpp
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Resource/IArchiveReader.cpp
        src/Resource/MpqArchive.cpp
        src/Resource/CascArchive.cpp
        src/Resource/FileSystemArchive.cpp
    )

    target_include_directories(CampaignCatalogRuntimeTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    target_link_libraries(CampaignCatalogRuntimeTest PRIVATE
        ${StormLib_TARGET}
        ${CascLib_TARGET}
    )

    target_compile_definitions(CampaignCatalogRuntimeTest PRIVATE
        $<$<BOOL:${HAS_STORMLIB}>:HAS_STORMLIB>
        $<$<BOOL:${HAS_STORMLIB}>:STORMLIB_NO_AUTO_LINK>
        $<$<BOOL:${HAS_STORMLIB}>:__STORMLIB_NO_STATIC_LINK__>
        $<$<BOOL:${HAS_CASCLIB}>:HAS_CASCLIB>
    )

    if(MSVC)
        target_compile_options(CampaignCatalogRuntimeTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(CampaignCatalogRuntimeTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(CampaignCatalogRuntimeTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()


option(BUILD_ECS_FOUNDATION_MODERNIZATION_TEST "Build ECS foundation modernization regression test" ON)
if(BUILD_ECS_FOUNDATION_MODERNIZATION_TEST)
    add_executable(EcsFoundationModernizationTest
        tests/Regression/EcsFoundationModernizationTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/UpgradeSystem.cpp
    )

    target_include_directories(EcsFoundationModernizationTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(EcsFoundationModernizationTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(EcsFoundationModernizationTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
endif()


option(BUILD_PHASE2_ENGINE_MODERNIZATION_TEST "Build Phase 2 engine modernization regression test" ON)
if(BUILD_PHASE2_ENGINE_MODERNIZATION_TEST)
    add_executable(Phase2EngineModernizationTest
        tests/Regression/Phase2EngineModernizationTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/BuildingSystem.cpp
        src/Game/Systems/CombatSystem.cpp
        src/Game/Systems/BulletSystem.cpp
        src/Game/Systems/AnimationSystem.cpp
        src/Game/Systems/CommandSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/TransportSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/IScriptVM.cpp
    )

    target_include_directories(Phase2EngineModernizationTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_options(Phase2EngineModernizationTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(Phase2EngineModernizationTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
endif()

foreach(_StarCraftRegressionTest IN ITEMS
    Phase2EngineModernizationTest
    EcsFoundationModernizationTest
    AnimationStateTest
    AnimationRuntimeDirectionTest
    MovementCommandTest
    NavigationPathfindingTest
    NavigationDeterminismTest
    NavigationStressTest
    CommandSystemOrderTest
    CombatRulesTest
    SpellVisualStateTest
    HarvestSystemCommandTest
    ProductionEconomyTest
    ZergCreepRegressionTest
    BuildingPlacementTest
    SelectionFootprintTest
    SpriteImageDataTest
    CursorTargetingTest
    MouseCoordinateTest
    CameraEdgeScrollTest
    IScriptOverlayTest
    CampaignCatalogRuntimeTest
    HudMinimapGeometryTest
    FogOfWarStatusTest
    HudCommandMetadataTest
    ChkStringEncodingTest
    SmartClickCommandTest
    SelectionInteractionTest
    RaceDefinitionMappingTest
    AIBrainStrategyTest
    MatchOutcomeTest
    OggVorbisDecodeTest
    TransportSystemTest )
    if(TARGET ${_StarCraftRegressionTest})
        add_test(NAME ${_StarCraftRegressionTest} COMMAND ${_StarCraftRegressionTest})
        set_tests_properties(${_StarCraftRegressionTest} PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        if("${_StarCraftRegressionTest}" STREQUAL "CampaignCatalogRuntimeTest")
            set_tests_properties(CampaignCatalogRuntimeTest PROPERTIES
                ENVIRONMENT "STARCRAFT_CAMPAIGN_LONG_AUDIT_SECONDS=0")
        endif()
        if("${_StarCraftRegressionTest}" STREQUAL "NavigationStressTest")
            set_tests_properties(NavigationStressTest PROPERTIES
                LABELS "stress" TIMEOUT 600)
        endif()
    endif()
endforeach()

option(BUILD_GAME_CLIENT "Build the game client" ON)
option(BUILD_RUNTIME_SMOKE_TEST "Register the real StarCraft client runtime smoke test" ON)

if(BUILD_GAME_CLIENT)
    set(STARCRAFT_CLIENT_SOURCES
        src/Main/GameMain.cpp
        src/PlatformBackend.cpp
        src/Core/Platform/Platform.cpp
        src/Core/Utils/Logger.cpp
        src/Core/Audio/AudioSystem.cpp
        src/Render/Renderer.cpp
        src/Render/Shader2D.cpp
        src/Render/BitmapFont.cpp
        src/Render/Draw2D.cpp
        src/Render/ParticleSystem.cpp
        src/Resource/Palette.cpp
        src/Resource/AssetCacheViews.cpp
        src/Resource/GrpToGpu.cpp
        src/Resource/DdsGrpDecoder.cpp
        src/Resource/RgbaImageFileDecoder.cpp
        src/Resource/PortraitVideoDecoder.cpp
        src/Resource/ResourceCache.cpp
        src/Resource/ResourceCacheUI.cpp
        src/Resource/ResourceCacheSprites.cpp
        src/Resource/ResourceCacheTilesetImages.cpp
        src/Resource/ResourceCachePortrait.cpp
        src/Resource/IArchiveReader.cpp
        src/Resource/MpqArchive.cpp
        src/Resource/CascArchive.cpp
        src/Resource/FileSystemArchive.cpp
        src/Resource/GameDataLoader.cpp
        src/Resource/GrpDecoder.cpp
        src/Resource/Cv5Decoder.cpp
        src/Resource/Vf4Decoder.cpp
        src/Resource/DatDecoder.cpp
        src/Resource/TblDecoder.cpp
        src/Game/Map/ChkParser.cpp
        src/Client/MapLoader.cpp
        src/Client/ClientRuntime.cpp
        src/Game/IScriptVM.cpp
        src/Client/TilesetRenderer.cpp
        src/Client/EntityRenderer.cpp
        src/Client/CameraController.cpp
        src/Client/Selection.cpp
        src/Resource/ModernAssetRegistry.cpp
        src/Game/Systems/DeclarativeAnimationPlayer.cpp
        src/Render/EnvironmentAndTerrainPipeline.cpp
        src/Render/SokolPBRDispatcher.cpp
        "src/UI/GlueCampaignData.cpp"
        "src/UI/Hud.cpp"
        "src/UI/HudCommandPanel.cpp"
        "src/UI/HudContext.cpp"
        "src/UI/HudInfoPanel.cpp"
        "src/UI/HudInfoPanelState.cpp"
        "src/UI/HudLayout.cpp"
        "src/UI/HudMinimap.cpp"
        "src/UI/HudResources.cpp"
        "src/Main/AppRuntime.cpp"
        "src/Main/CampaignPresentation.cpp"
        "src/Main/GameMenuPresentation.cpp"
        "src/Main/GameController.cpp"
        "src/Main/GameGuiPresentation.cpp"
        "src/Main/GameLoop/CursorTypes.cpp"
        "src/Main/GameLoop/CursorManager.cpp"
        "src/Main/GameLoop/GameSaveState.cpp"
        "src/Main/GameLoop/InputHandler.cpp"
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Game/Systems/CommandSystem.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/BuildingSystem.cpp
        src/Game/Systems/ProductionSystem.cpp
        src/Game/Systems/CombatSystem.cpp
        src/Game/Systems/BulletSystem.cpp
        src/Game/Systems/FogOfWarSystem.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Map/Map.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/AI/AIBrain.cpp
        src/Game/Campaign/CampaignController.cpp
        src/Game/Campaign/CampaignMissionCatalog.cpp
        src/Game/Trigger/TriggerVM.cpp
        src/Game/AI/BehaviorTreeRuntime.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Systems/ZergSystem.cpp
        src/Game/Systems/ProtossSystem.cpp
        src/Game/Systems/NukeAndSpiderMineSystem.cpp
        src/Game/Systems/SpellSystem.cpp
        src/Game/Systems/AnimationSystem.cpp
        src/Game/Systems/TransportSystem.cpp
    )

    if(ENABLE_GUI_UI AND HAS_GUI)
        list(APPEND STARCRAFT_CLIENT_SOURCES
            "src/UI/GuiHudHost.cpp"
            "src/UI/MainGui.cpp"
            "src/UI/GlueAnimatedImage.cpp"
            "src/UI/GlueGuiHost.cpp"
            "src/UI/GlueMenuController.cpp"
            "${GUI_PACKAGE_RENDER_DIR}/D3D11RenderContext/Src/D3D11MediaPlayer.cpp")
    endif()

    add_executable(StarCraft ${STARCRAFT_CLIENT_SOURCES})
    source_group(TREE ${CMAKE_SOURCE_DIR} PREFIX "Client Source Files" FILES ${STARCRAFT_CLIENT_SOURCES})
    target_include_directories(StarCraft PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)
    if(ENABLE_GUI_UI AND HAS_GUI)
        target_include_directories(StarCraft PRIVATE
            ${GUI_INCLUDE_DIR}
            ${GUI_PACKAGE_APP_DIR}/ApplicationLauncher/Include
            ${GUI_PACKAGE_APP_DIR}/DisplayLauncher/Include
            ${GUI_PACKAGE_APP_DIR}/Display/Include
            ${GUI_PACKAGE_APP_DIR}/Launcher/Include
            ${GUI_PACKAGE_APP_DIR}/MediaElement/Include
            ${GUI_PACKAGE_APP_DIR}/Providers/Include
            ${GUI_PACKAGE_APP_DIR}/Theme/Include
            ${GUI_PACKAGE_RENDER_DIR}/D3D11RenderDevice/Include
            ${GUI_PACKAGE_RENDER_DIR}/D3D11RenderContext/Src
            ${GUI_PACKAGE_RENDER_DIR}/GLRenderDevice/Include
            ${GUI_PACKAGE_RENDER_DIR}/GLRenderContext/Include
            ${GUI_PACKAGE_RENDER_DIR}/RenderContext/Include
        )
    endif()
    target_compile_definitions(StarCraft PRIVATE
        SOKOL_D3D11
        $<$<BOOL:${HAS_STORMLIB}>:HAS_STORMLIB>
        $<$<BOOL:${HAS_STORMLIB}>:STORMLIB_NO_AUTO_LINK>
        $<$<BOOL:${HAS_STORMLIB}>:__STORMLIB_NO_STATIC_LINK__>
        $<$<BOOL:${HAS_CASCLIB}>:HAS_CASCLIB>
        $<$<BOOL:${HAS_LIBVPX}>:HAS_LIBVPX>
        $<$<BOOL:${ENABLE_GUI_UI}>:ENABLE_GUI_UI>
        $<$<BOOL:${HAS_GUI}>:HAS_GUI>
        $<$<BOOL:${HAS_GUI}>:NS_APP_FRAMEWORK>
        $<$<BOOL:${HAS_GUI}>:NS_DEBUG>
        $<$<BOOL:${HAS_GUI}>:NS_MINIMUM_LOG_LEVEL=1>
    )

    if(MSVC)
        target_compile_options(StarCraft PRIVATE /W4 /utf-8 /permissive- /Zi)
        target_link_options(StarCraft PRIVATE /DEBUG /INCREMENTAL:NO)
        target_compile_definitions(StarCraft PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(StarCraft PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(WIN32)
        target_link_libraries(StarCraft PRIVATE
            dbghelp
            d3d11 dxgi d3dcompiler user32 gdi32 shell32 mfplat mfreadwrite mfuuid
            ${StormLib_TARGET}
            ${CascLib_TARGET}
            ${ANGELSCRIPT_TARGET}
            ${LIBVPX_TARGET})
        if(ENABLE_GUI_UI AND HAS_GUI)
            target_link_libraries(StarCraft PRIVATE
                "${GUI_APP_LIB}"
                "${GUI_CORE_LIB}"
                shlwapi dwmapi opengl32 ole32 uuid comdlg32 advapi32)

            set(_STARCRAFT_CLIENT_GUI_DLLS
                "${GUI_CORE_DLL}"
                "${GUI_APP_DLL}")
            set(_STARCRAFT_CLIENT_GUI_DLLS_TO_COPY)
            foreach(_AeroDll IN LISTS _STARCRAFT_CLIENT_GUI_DLLS)
                if(EXISTS "${_AeroDll}")
                    list(APPEND _STARCRAFT_CLIENT_GUI_DLLS_TO_COPY "${_AeroDll}")
                endif()
            endforeach()

            if(_STARCRAFT_CLIENT_GUI_DLLS_TO_COPY)
                add_custom_command(TARGET StarCraft POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:StarCraft>
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${_STARCRAFT_CLIENT_GUI_DLLS_TO_COPY}
                        $<TARGET_FILE_DIR:StarCraft>
                    COMMAND_EXPAND_LISTS)
            endif()
        endif()
        set(_STARCRAFT_CLIENT_RUNTIME_DLLS)
        if(EXISTS "${_VCPKG_INSTALLED_HINT}/debug/bin/zlibd1.dll")
            list(APPEND _STARCRAFT_CLIENT_RUNTIME_DLLS
                "$<$<CONFIG:Debug>:${_VCPKG_INSTALLED_HINT}/debug/bin/zlibd1.dll>")
        endif()
        if(EXISTS "${_VCPKG_INSTALLED_HINT}/debug/bin/bz2d.dll")
            list(APPEND _STARCRAFT_CLIENT_RUNTIME_DLLS
                "$<$<CONFIG:Debug>:${_VCPKG_INSTALLED_HINT}/debug/bin/bz2d.dll>")
        endif()
        if(EXISTS "${_VCPKG_INSTALLED_HINT}/bin/zlib1.dll")
            list(APPEND _STARCRAFT_CLIENT_RUNTIME_DLLS
                "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:${_VCPKG_INSTALLED_HINT}/bin/zlib1.dll>")
        endif()
        if(EXISTS "${_VCPKG_INSTALLED_HINT}/bin/bz2.dll")
            list(APPEND _STARCRAFT_CLIENT_RUNTIME_DLLS
                "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:${_VCPKG_INSTALLED_HINT}/bin/bz2.dll>")
        endif()
        if(_STARCRAFT_CLIENT_RUNTIME_DLLS)
            add_custom_command(TARGET StarCraft POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_RUNTIME_DLLS:StarCraft>"
                    ${_STARCRAFT_CLIENT_RUNTIME_DLLS}
                    $<TARGET_FILE_DIR:StarCraft>
                COMMAND_EXPAND_LISTS
            )
        endif()
    else()
        find_package(OpenGL REQUIRED)
        find_package(X11 REQUIRED)
        find_library(XI_LIBRARY Xi)
        find_library(XCURSOR_LIBRARY Xcursor)
        target_include_directories(StarCraft PRIVATE ${OPENGL_INCLUDE_DIRS} ${X11_INCLUDE_DIR})
        target_link_libraries(StarCraft PRIVATE
            ${OPENGL_LIBRARIES}
            ${X11_LIBRARIES}
            ${XI_LIBRARY}
            ${XCURSOR_LIBRARY}
            ${StormLib_TARGET}
            ${CascLib_TARGET}
            ${ANGELSCRIPT_TARGET}
            ${LIBVPX_TARGET}
            dl pthread m asound)
    endif()

    if(BUILD_RUNTIME_SMOKE_TEST)
        add_test(NAME StarCraftRuntimeSmoke
            COMMAND ${CMAKE_COMMAND}
                -DSTARCRAFT_EXE=$<TARGET_FILE:StarCraft>
                -DSTARCRAFT_WORKDIR=${CMAKE_SOURCE_DIR}
                -DSTARCRAFT_SMOKE_LOG_DIR=${CMAKE_BINARY_DIR}/Testing/RuntimeSmoke
                -P ${CMAKE_SOURCE_DIR}/cmake/StarCraftRuntimeSmoke.cmake)
        set_tests_properties(StarCraftRuntimeSmoke PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 60
            LABELS "runtime")

        add_test(NAME StarCraftInteractionSmoke
            COMMAND ${CMAKE_COMMAND}
                -DSTARCRAFT_EXE=$<TARGET_FILE:StarCraft>
                -DSTARCRAFT_WORKDIR=${CMAKE_SOURCE_DIR}
                -DSTARCRAFT_SMOKE_LOG_DIR=${CMAKE_BINARY_DIR}/Testing/RuntimeSmoke
                -P ${CMAKE_SOURCE_DIR}/cmake/StarCraftInteractionSmoke.cmake)
        set_tests_properties(StarCraftInteractionSmoke PROPERTIES
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            TIMEOUT 120
            LABELS "runtime")
    endif()
endif()


option(BUILD_PHASE3_RENDER_PIPELINE_TEST "Build Phase 3 64-bit SortKey RenderQueue2D regression test" ON)
if(BUILD_PHASE3_RENDER_PIPELINE_TEST)
    add_executable(Phase3RenderPipelineTest
        tests/Regression/Phase3RenderPipelineTest.cpp
        src/Client/EntityRenderer.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/Utils/Logger.cpp
    )

    starcraft_configure_test_target(Phase3RenderPipelineTest)

    add_test(NAME Phase3RenderPipelineTest COMMAND Phase3RenderPipelineTest)
endif()




option(BUILD_PHASE10_TO_13_ENGINE_MODERNIZATION_TEST "Build Phase 10-13 Godot/Unity engine modernization regression test" ON)
if(BUILD_PHASE10_TO_13_ENGINE_MODERNIZATION_TEST)
    add_executable(Phase10To13EngineModernizationTest
        tests/Regression/Phase10To13EngineModernizationTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/BuildingSystem.cpp
        src/Game/Systems/CommandSystem.cpp
        src/Game/Systems/CombatSystem.cpp
        src/Game/Systems/BulletSystem.cpp
        src/Game/Systems/AnimationSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/ProductionSystem.cpp
        src/Game/Systems/TransportSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/IScriptVM.cpp
    )

    target_include_directories(Phase10To13EngineModernizationTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(Phase10To13EngineModernizationTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(Phase10To13EngineModernizationTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(Phase10To13EngineModernizationTest PRIVATE -Wall -Wextra -Wpedantic -pthread)
        target_link_options(Phase10To13EngineModernizationTest PRIVATE -pthread)
    endif()

    add_test(NAME Phase10To13EngineModernizationTest COMMAND Phase10To13EngineModernizationTest)
endif()

option(BUILD_PHASE6_ENGINE_ARCHITECTURE_REFACTOR_TEST "Build Phase 6 Godot/Unity engine architecture refactoring regression test" ON)
if(BUILD_PHASE6_ENGINE_ARCHITECTURE_REFACTOR_TEST)
    add_executable(Phase6EngineArchitectureRefactorTest
        tests/Regression/Phase6EngineArchitectureRefactorTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
    )

    target_include_directories(Phase6EngineArchitectureRefactorTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party)

    target_compile_definitions(Phase6EngineArchitectureRefactorTest PRIVATE
        SOKOL_DUMMY_BACKEND
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)

    if(MSVC)
        target_compile_options(Phase6EngineArchitectureRefactorTest PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(Phase6EngineArchitectureRefactorTest PRIVATE -Wall -Wextra -Wpedantic -pthread)
        target_link_options(Phase6EngineArchitectureRefactorTest PRIVATE -pthread)
    endif()

    add_test(NAME Phase6EngineArchitectureRefactorTest COMMAND Phase6EngineArchitectureRefactorTest)
endif()

option(BUILD_PHASE5_JOB_SYSTEM_PARALLEL_TEST "Build Phase 5 JobSystem and CSR SpatialGrid regression test" ON)
if(BUILD_PHASE5_JOB_SYSTEM_PARALLEL_TEST)
    add_executable(Phase5JobSystemParallelTest
        tests/Regression/Phase5JobSystemParallelTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/FogOfWarSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
    )

    target_include_directories(Phase5JobSystemParallelTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(Phase5JobSystemParallelTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(Phase5JobSystemParallelTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(Phase5JobSystemParallelTest PRIVATE -Wall -Wextra -Wpedantic -pthread)
        target_link_options(Phase5JobSystemParallelTest PRIVATE -pthread)
    endif()

    add_test(NAME Phase5JobSystemParallelTest COMMAND Phase5JobSystemParallelTest)
endif()


option(BUILD_PHASE4_HUD_MVVM_BINDING_TEST "Build Phase 4 HUD MVVM reactive data-binding regression test" ON)
if(BUILD_PHASE4_HUD_MVVM_BINDING_TEST)
    add_executable(Phase4HudMvvmBindingTest
        tests/Regression/Phase4HudMvvmBindingTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/BuildingSystem.cpp
        src/Game/Systems/CommandSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/ProductionSystem.cpp
        src/Game/Systems/ProtossSystem.cpp
        src/Game/Systems/SpellSystem.cpp
        src/Game/Systems/NukeAndSpiderMineSystem.cpp
        src/Game/Systems/TransportSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Systems/ZergSystem.cpp
        src/UI/HudCommandPanel.cpp
        src/UI/HudContext.cpp
        src/UI/HudInfoPanelState.cpp
    )

    starcraft_configure_test_target(Phase4HudMvvmBindingTest)

    add_test(NAME Phase4HudMvvmBindingTest COMMAND Phase4HudMvvmBindingTest)
endif()

# Config Summary
message(STATUS "")
message(STATUS "================ Build Configuration ================")
message(STATUS "  raylib:    ${HAS_RAYLIB}  (removed)")
message(STATUS "  StormLib:  ${HAS_STORMLIB}  (MPQ)")
message(STATUS "  CascLib:   ${HAS_CASCLIB}  (CASC)")
message(STATUS "  AngelScript: ${HAS_ANGELSCRIPT}")
message(STATUS "  Generator: ${CMAKE_GENERATOR}")
message(STATUS "  C++:       ${CMAKE_CXX_STANDARD}")
message(STATUS "=====================================================")
message(STATUS "")

option(BUILD_MODERN_ENGINE_VERIFICATION_TEST "Build modern engine & cloud assets verification test" ON)
if(BUILD_MODERN_ENGINE_VERIFICATION_TEST)
    add_executable(ModernEngineVerificationTest
        tests/Regression/ModernEngineVerificationTest.cpp
        $<TARGET_OBJECTS:StarCraftCoreObj>
        src/Game/IScriptVM.cpp
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/AI/AIBrain.cpp
        src/Game/AI/BehaviorTreeRuntime.cpp
        src/Game/Trigger/TriggerVM.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Systems/DeclarativeAnimationPlayer.cpp
        src/Game/Systems/AnimationSystem.cpp
        src/Game/Systems/BuildingSystem.cpp
        src/Game/Systems/BulletSystem.cpp
        src/Game/Systems/CombatSystem.cpp
        src/Game/Systems/CommandSystem.cpp
        src/Game/Systems/FogOfWarSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/NukeAndSpiderMineSystem.cpp
        src/Game/Systems/ProductionSystem.cpp
        src/Game/Systems/ProtossSystem.cpp
        src/Game/Systems/SpellSystem.cpp
        src/Game/Systems/TransportSystem.cpp
        src/Game/Systems/ZergSystem.cpp
        src/Resource/GameDataLoader.cpp
        src/Resource/IArchiveReader.cpp
        src/Resource/FileSystemArchive.cpp
        src/Resource/DatDecoder.cpp
        src/Resource/Palette.cpp
        src/Resource/TblDecoder.cpp
        src/Resource/Cv5Decoder.cpp
        src/Resource/Vf4Decoder.cpp
        src/Resource/RgbaImageFileDecoder.cpp
        src/Resource/ModernAssetRegistry.cpp
        src/Render/Draw2D.cpp
        src/Render/Shader2D.cpp
        src/Render/BitmapFont.cpp
        src/Render/EnvironmentAndTerrainPipeline.cpp
        src/Render/SokolPBRDispatcher.cpp
    )
    target_include_directories(ModernEngineVerificationTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party
    )
    target_compile_definitions(ModernEngineVerificationTest PRIVATE
        SOKOL_DUMMY_BACKEND
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
        target_compile_options(ModernEngineVerificationTest PRIVATE /utf-8 /permissive-)
        target_link_libraries(ModernEngineVerificationTest PRIVATE windowscodecs ole32)
    endif()
    add_test(NAME ModernEngineVerificationTest COMMAND ModernEngineVerificationTest)
    set_tests_properties(ModernEngineVerificationTest PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# ============================================================================
# Modular Engine & Simulation Object Libraries (Eliminate Redundant Compilation)
# ============================================================================

add_library(StarCraftSimulationObj OBJECT
    src/Game/AI/BehaviorTreeRuntime.cpp
    src/Game/IScriptVM.cpp
    src/Game/Map/ChkParser.cpp
    src/Game/Map/Map.cpp
    src/Game/Pathfinding/AStar.cpp
    src/Game/Races/RaceDef.cpp
    src/Game/Systems/AnimationSystem.cpp
    src/Game/Systems/BuildingSystem.cpp
    src/Game/Systems/BulletSystem.cpp
    src/Game/Systems/CombatSystem.cpp
    src/Game/Systems/CommandSystem.cpp
    src/Game/Systems/DeclarativeAnimationPlayer.cpp
    src/Game/Systems/FogOfWarSystem.cpp
    src/Game/Systems/HarvestSystem.cpp
    src/Game/Systems/MovementSystem.cpp
    src/Game/Systems/NukeAndSpiderMineSystem.cpp
    src/Game/Systems/ProductionSystem.cpp
    src/Game/Systems/ProtossSystem.cpp
    src/Game/Systems/SpellSystem.cpp
    src/Game/Systems/TransportSystem.cpp
    src/Game/Systems/UpgradeSystem.cpp
    src/Game/Systems/ZergSystem.cpp
    src/Resource/ModernAssetRegistry.cpp
)
target_include_directories(StarCraftSimulationObj PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)
target_compile_definitions(StarCraftSimulationObj PRIVATE
    SOKOL_DUMMY_BACKEND
    _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(StarCraftSimulationObj PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(StarCraftSimulationObj PRIVATE -Wall -Wextra -Wpedantic -pthread)
endif()

function(starcraft_add_modern_phase_test TARGET_NAME TEST_SOURCE)
    add_executable(${TARGET_NAME}
        ${TEST_SOURCE}
        $<TARGET_OBJECTS:StarCraftCoreObj>
        $<TARGET_OBJECTS:StarCraftSimulationObj>
        ${ARGN}
    )
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE
        SOKOL_DUMMY_BACKEND
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic -pthread)
        target_link_options(${TARGET_NAME} PRIVATE -pthread)
    endif()
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    set_tests_properties(${TARGET_NAME} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endfunction()

option(BUILD_PHASE14_FULL_STACK_ENGINE_REFACTOR_TEST "Build Phase 14 full-stack engine modernization & rollback verification test" ON)
if(BUILD_PHASE14_FULL_STACK_ENGINE_REFACTOR_TEST)
    starcraft_add_modern_phase_test(Phase14FullStackEngineRefactorTest
        tests/Regression/Phase14FullStackEngineRefactorTest.cpp)
endif()

option(BUILD_PHASE15_TO_18_MODERN_ENGINE_REFACTOR_TEST "Build Phase 15-18 shared spatial broadphase, multi-world Protoss/Zerg, and generational Selection regression test" ON)
if(BUILD_PHASE15_TO_18_MODERN_ENGINE_REFACTOR_TEST)
    starcraft_add_modern_phase_test(Phase15To18ModernEngineRefactorTest
        tests/Regression/Phase15To18ModernEngineRefactorTest.cpp
        src/Client/Selection.cpp)
endif()

option(BUILD_PHASE19_TO_22_MODERN_ENGINE_REFACTOR_TEST "Build Phase 19-22 CommandSystem/SpellSystem spatial broadphase, EntityRenderer retained CanvasItemRIDs, and cached queries regression test" ON)
if(BUILD_PHASE19_TO_22_MODERN_ENGINE_REFACTOR_TEST)
    starcraft_add_modern_phase_test(Phase19To22ModernEngineRefactorTest
        tests/Regression/Phase19To22ModernEngineRefactorTest.cpp
        src/Client/EntityRenderer.cpp)
endif()

option(BUILD_PHASE23_TO_26_MODERN_ENGINE_REFACTOR_TEST "Build Phase 23-26 TriggerVM spatial broadphase, authoritative ECS save/load, and declarative animation bridge regression test" ON)
if(BUILD_PHASE23_TO_26_MODERN_ENGINE_REFACTOR_TEST)
    starcraft_add_modern_phase_test(Phase23To26ModernEngineRefactorTest
        tests/Regression/Phase23To26ModernEngineRefactorTest.cpp
        src/Game/Trigger/TriggerVM.cpp
        src/Main/GameLoop/GameSaveState.cpp)
endif()

option(BUILD_PHASE27_TO_30_MODERN_ENGINE_REFACTOR_TEST "Build Phase 27-30 Movement, Harvest, Production, Selection, CursorManager, and Transport/Upgrade spatial & cached queries regression test" ON)
if(BUILD_PHASE27_TO_30_MODERN_ENGINE_REFACTOR_TEST)
    starcraft_add_modern_phase_test(Phase27To30ModernEngineRefactorTest
        tests/Regression/Phase27To30ModernEngineRefactorTest.cpp
        src/Client/Selection.cpp
        src/Main/GameLoop/CursorManager.cpp
        src/Main/GameLoop/CursorTypes.cpp)
endif()

option(BUILD_PHASE31_TO_36_ENGINE_FOUNDATION_TEST "Build Phase 31-36 general-purpose Engine Foundation, GenericWorldSnapshot, EngineApp, and cloud SystemAssetBridge test" ON)
if(BUILD_PHASE31_TO_36_ENGINE_FOUNDATION_TEST)
    starcraft_add_modern_phase_test(Phase31To36EngineFoundationAndSimplificationTest
        tests/Regression/Phase31To36EngineFoundationAndSimplificationTest.cpp)
endif()

option(BUILD_PHASE37_TO_42_ENGINE_EVOLUTION_TEST "Build Phase 37-42 Engine Evolution & Simplification test" ON)
if(BUILD_PHASE37_TO_42_ENGINE_EVOLUTION_TEST)
    starcraft_add_modern_phase_test(Phase37To42EngineEvolutionTest
        tests/Regression/Phase37To42EngineEvolutionTest.cpp
        src/Main/GameLoop/GameSaveState.cpp)
endif()

option(BUILD_PHASE43_TO_48_ENGINE_GENERALIZATION_TEST "Build Phase 43-48 Engine Generalization & Simplification test" ON)
if(BUILD_PHASE43_TO_48_ENGINE_GENERALIZATION_TEST)
    starcraft_add_modern_phase_test(Phase43To48EngineGeneralizationAndSimplificationTest
        tests/Regression/Phase43To48EngineGeneralizationAndSimplificationTest.cpp)
endif()

option(BUILD_PHASE49_TO_54_ENGINE_ZERO_DEPENDENCY_TEST "Build Phase 49-54 Zero-Dependency Engine & Unification test" ON)
if(BUILD_PHASE49_TO_54_ENGINE_ZERO_DEPENDENCY_TEST)
    starcraft_add_modern_phase_test(Phase49To54EngineZeroDependencyAndUnificationTest
        tests/Regression/Phase49To54EngineZeroDependencyAndUnificationTest.cpp
        src/Game/AI/AIBrain.cpp
        src/Resource/Palette.cpp)
endif()

# ============================================================================
# Phase 55-60: General-Purpose Engine Foundation Library + Game Project Split
# ============================================================================
add_library(EngineFoundationObj OBJECT
    src/Core/ECS/EntityManager.cpp
    src/Core/ECS/SystemManager.cpp
    src/Core/Utils/Logger.cpp
    src/Resource/IArchiveReader.cpp
    src/Resource/FileSystemArchive.cpp
    src/Resource/Cv5Decoder.cpp
    src/Resource/Vf4Decoder.cpp
    src/Resource/TblDecoder.cpp
    src/Resource/Palette.cpp
    src/Resource/ModernAssetRegistry.cpp
    src/Render/ParticleSystem.cpp
)
target_include_directories(EngineFoundationObj PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
if(MSVC)
    target_compile_options(EngineFoundationObj PRIVATE /W4 /utf-8 /permissive-)
    target_compile_definitions(EngineFoundationObj PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(EngineFoundationObj PRIVATE -Wall -Wextra -Wpedantic -pthread)
endif()

option(BUILD_PHASE55_TO_60_ENGINE_PROJECT_SPLIT_TEST "Build Phase 55-60 Engine Foundation + Game Project Split, VFS, TilemapServer2D & ScenarioTriggerServer test" ON)
if(BUILD_PHASE55_TO_60_ENGINE_PROJECT_SPLIT_TEST)
    starcraft_add_modern_phase_test(Phase55To60EngineProjectSplitAndVfsTilemapTest
        tests/Regression/Phase55To60EngineProjectSplitAndVfsTilemapTest.cpp
        src/Resource/Cv5Decoder.cpp)
endif()
