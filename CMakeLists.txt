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

# AngelScript (optional script runtime)
option(BUILD_ANGELSCRIPT "Build AngelScript third-party scripting library" OFF)
set(ANGELSCRIPT_TARGET "")
set(HAS_ANGELSCRIPT FALSE)
if(BUILD_ANGELSCRIPT)
    set(ANGELSCRIPT_CMAKE_DIR "${CMAKE_SOURCE_DIR}/third_party/AngelScript/angelscript/projects/cmake")
    if(EXISTS "${ANGELSCRIPT_CMAKE_DIR}/CMakeLists.txt")
        add_subdirectory(
            ${ANGELSCRIPT_CMAKE_DIR}
            "${CMAKE_BINARY_DIR}/third_party/angelscript"
        )
        if(TARGET angelscript)
            set(ANGELSCRIPT_TARGET angelscript)
            set(HAS_ANGELSCRIPT TRUE)
        endif()
    endif()
endif()

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

# MpqListTest (legacy raylib dependent test removed).
# Keep the tool available only for archive parsing smoke checks.
option(BUILD_MPQ_LIST_TEST "Build MPQ file listing test tool" OFF)
if(BUILD_MPQ_LIST_TEST)
    add_executable(MpqListTest
        tests/MpqListTest/MpqListTest.cpp
        src/Resource/MpqArchive.cpp
        src/Core/Utils/Logger.cpp
    )

    target_include_directories(MpqListTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

    target_link_libraries(MpqListTest PRIVATE
        ${StormLib_TARGET}
    )

    target_compile_definitions(MpqListTest PRIVATE
        $<$<BOOL:${HAS_STORMLIB}>:HAS_STORMLIB>
        $<$<BOOL:${HAS_STORMLIB}>:STORMLIB_NO_AUTO_LINK>
        $<$<BOOL:${HAS_STORMLIB}>:__STORMLIB_NO_STATIC_LINK__>
    )

    if(MSVC)
        target_compile_options(MpqListTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(MpqListTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(MpqListTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_MAP_PARSE_TEST "Build map parsing test tool" OFF)
if(BUILD_MAP_PARSE_TEST)
    add_executable(MapParseTest
        tests/MapParseTest/MapParseTest.cpp
        src/Resource/MpqArchive.cpp
        src/Core/Utils/Logger.cpp
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
    )

    target_include_directories(MapParseTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

    target_link_libraries(MapParseTest PRIVATE
        ${StormLib_TARGET}
    )

    target_compile_definitions(MapParseTest PRIVATE
        $<$<BOOL:${HAS_STORMLIB}>:HAS_STORMLIB>
        $<$<BOOL:${HAS_STORMLIB}>:STORMLIB_NO_AUTO_LINK>
        $<$<BOOL:${HAS_STORMLIB}>:__STORMLIB_NO_STATIC_LINK__>
    )

    if(MSVC)
        target_compile_options(MapParseTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(MapParseTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(MapParseTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_MAP_RESOURCE_ANALYZER "Build map resource analysis tool" OFF)
if(BUILD_MAP_RESOURCE_ANALYZER)
    add_executable(MapResourceAnalyzer
        tests/MapResourceAnalyzer/MapResourceAnalyzer.cpp
        src/Resource/MpqArchive.cpp
        src/Resource/CascArchive.cpp
        src/Resource/IArchiveReader.cpp
        src/Resource/FileSystemArchive.cpp
        src/Resource/GameDetector.cpp
        src/Resource/GameDataLoader.cpp
        src/Resource/DatDecoder.cpp
        src/Resource/TblDecoder.cpp
        src/Resource/PalDecoder.cpp
        src/Game/IScriptVM.cpp
        src/Game/Map/ChkParser.cpp
        src/Core/Utils/Logger.cpp
    )

    target_include_directories(MapResourceAnalyzer PRIVATE ${CMAKE_SOURCE_DIR}/src)

    target_link_libraries(MapResourceAnalyzer PRIVATE
        ${StormLib_TARGET}
        ${CascLib_TARGET}
    )

    target_compile_definitions(MapResourceAnalyzer PRIVATE
        $<$<BOOL:${HAS_STORMLIB}>:HAS_STORMLIB>
        $<$<BOOL:${HAS_STORMLIB}>:STORMLIB_NO_AUTO_LINK>
        $<$<BOOL:${HAS_STORMLIB}>:__STORMLIB_NO_STATIC_LINK__>
        $<$<BOOL:${HAS_CASCLIB}>:HAS_CASCLIB>
    )

    if(MSVC)
        target_compile_options(MapResourceAnalyzer PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(MapResourceAnalyzer PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(MapResourceAnalyzer PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_ISCRIPT_ANGELSCRIPT_TOOL "Build IScript to AngelScript source exporter" ON)
if(BUILD_ISCRIPT_ANGELSCRIPT_TOOL)
    add_executable(IScriptToAngelScript
        src/Game/IScriptToAngelScript.cpp
        src/Game/IScriptAngelScriptExporter.cpp
        src/Game/IScriptVM.cpp
        src/Core/Utils/Logger.cpp
    )

    target_include_directories(IScriptToAngelScript PRIVATE
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(IScriptToAngelScript PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(IScriptToAngelScript PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(IScriptToAngelScript PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(IScriptAngelScriptExportTest
        src/Game/Regression/IScriptAngelScriptExportTest.cpp
        src/Game/IScriptAngelScriptExporter.cpp
        src/Game/IScriptVM.cpp
        src/Core/Utils/Logger.cpp
    )

    target_include_directories(IScriptAngelScriptExportTest PRIVATE
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(IScriptAngelScriptExportTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(IScriptAngelScriptExportTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(IScriptAngelScriptExportTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(HAS_ANGELSCRIPT)
        add_executable(IScriptAngelScriptCompileTest
            src/Game/Regression/IScriptAngelScriptCompileTest.cpp
            src/Game/IScriptAngelScriptExporter.cpp
            src/Game/IScriptVM.cpp
            src/Core/Utils/Logger.cpp
        )

        target_include_directories(IScriptAngelScriptCompileTest PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/third_party/AngelScript/angelscript/include)
        target_link_libraries(IScriptAngelScriptCompileTest PRIVATE
            ${ANGELSCRIPT_TARGET})

        if(MSVC)
            target_compile_options(IScriptAngelScriptCompileTest PRIVATE /W4 /utf-8 /permissive-)
            target_compile_definitions(IScriptAngelScriptCompileTest PRIVATE
                _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
        else()
            target_compile_options(IScriptAngelScriptCompileTest PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endif()

option(BUILD_COMBAT_RULES_TEST "Build combat rule regression test" ON)
if(BUILD_COMBAT_RULES_TEST)
    add_executable(CombatRulesTest
        src/Game/Regression/CombatRulesTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/IScriptVM.cpp
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/BulletSystem.cpp
        src/Game/Systems/CombatSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
    )

    target_include_directories(CombatRulesTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(CombatRulesTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(CombatRulesTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(CombatRulesTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_SPELL_VISUAL_STATE_TEST "Build spell visual state regression test" ON)
if(BUILD_SPELL_VISUAL_STATE_TEST)
    add_executable(SpellVisualStateTest
        src/Game/Regression/SpellVisualStateTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/SpellSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
    )

    target_include_directories(SpellVisualStateTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(SpellVisualStateTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(SpellVisualStateTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(SpellVisualStateTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_ANIMATION_STATE_TEST "Build animation state regression test" ON)
if(BUILD_ANIMATION_STATE_TEST)
    add_executable(AnimationStateTest
        src/Game/Regression/AnimationStateTest.cpp
    )

    target_include_directories(AnimationStateTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(AnimationStateTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(AnimationStateTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(AnimationStateTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_ANIMATION_RUNTIME_DIRECTION_TEST "Build animation runtime direction regression test" ON)
if(BUILD_ANIMATION_RUNTIME_DIRECTION_TEST)
    add_executable(AnimationRuntimeDirectionTest
        src/Game/Regression/AnimationRuntimeDirectionTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/IScriptVM.cpp
        src/Game/Systems/AnimationSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Races/RaceDef.cpp
    )

    target_include_directories(AnimationRuntimeDirectionTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(AnimationRuntimeDirectionTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(AnimationRuntimeDirectionTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(AnimationRuntimeDirectionTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_ISCRIPT_OVERLAY_TEST "Build iscript overlay regression test" ON)
if(BUILD_ISCRIPT_OVERLAY_TEST)
    add_executable(IScriptOverlayTest
        src/Game/Regression/IScriptOverlayTest.cpp
        src/Core/Utils/Logger.cpp
        src/Game/IScriptVM.cpp
    )

    target_include_directories(IScriptOverlayTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(IScriptOverlayTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(IScriptOverlayTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(IScriptOverlayTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_SPRITE_IMAGE_DATA_TEST "Build sprite/image data mapping regression test" ON)
if(BUILD_SPRITE_IMAGE_DATA_TEST)
    add_executable(SpriteImageDataTest
        src/Game/Regression/SpriteImageDataTest.cpp
        src/Core/Utils/Logger.cpp
        src/Game/IScriptVM.cpp
        src/Resource/CascArchive.cpp
        src/Resource/DatDecoder.cpp
        src/Resource/FileSystemArchive.cpp
        src/Resource/GameDataLoader.cpp
        src/Resource/GameDetector.cpp
        src/Resource/GrpDecoder.cpp
        src/Resource/IArchiveReader.cpp
        src/Resource/MpqArchive.cpp
        src/Resource/PalDecoder.cpp
        src/Resource/TblDecoder.cpp
    )

    target_include_directories(SpriteImageDataTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(SpriteImageDataTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(SpriteImageDataTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(SpriteImageDataTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_RACE_DEFINITION_MAPPING_TEST "Build race definition mapping regression test" ON)
if(BUILD_RACE_DEFINITION_MAPPING_TEST)
    add_executable(RaceDefinitionMappingTest
        src/Game/Regression/RaceDefinitionMappingTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/Races/RaceDef.cpp
    )

    target_include_directories(RaceDefinitionMappingTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(RaceDefinitionMappingTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(RaceDefinitionMappingTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(RaceDefinitionMappingTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_AI_BRAIN_STRATEGY_TEST "Build AI difficulty strategy regression test" ON)
if(BUILD_AI_BRAIN_STRATEGY_TEST)
    add_executable(AIBrainStrategyTest
        src/Game/Regression/AIBrainStrategyTest.cpp
    )

    target_include_directories(AIBrainStrategyTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(AIBrainStrategyTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(AIBrainStrategyTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(AIBrainStrategyTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_COMMAND_SYSTEM_ORDER_TEST "Build command system order legality regression test" ON)
if(BUILD_COMMAND_SYSTEM_ORDER_TEST)
    add_executable(CommandSystemOrderTest
        src/Game/Regression/CommandSystemOrderTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
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

    target_include_directories(CommandSystemOrderTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(CommandSystemOrderTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(CommandSystemOrderTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(CommandSystemOrderTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_HUD_MINIMAP_GEOMETRY_TEST "Build HUD minimap geometry regression test" ON)
if(BUILD_HUD_MINIMAP_GEOMETRY_TEST)
    add_executable(HudMinimapGeometryTest
        src/Game/Regression/HudMinimapGeometryTest.cpp
    )

    target_include_directories(HudMinimapGeometryTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(HudMinimapGeometryTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(HudMinimapGeometryTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(HudMinimapGeometryTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_FOG_OF_WAR_STATUS_TEST "Build fog of war status regression test" ON)
if(BUILD_FOG_OF_WAR_STATUS_TEST)
    add_executable(FogOfWarStatusTest
        src/Game/Regression/FogOfWarStatusTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/Map/Map.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/FogOfWarSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
    )

    target_include_directories(FogOfWarStatusTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(FogOfWarStatusTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(FogOfWarStatusTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(FogOfWarStatusTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_HUD_COMMAND_METADATA_TEST "Build HUD command metadata regression test" ON)
if(BUILD_HUD_COMMAND_METADATA_TEST)
    add_executable(HudCommandMetadataTest
        src/Game/Regression/HudCommandMetadataTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
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
        src/UI/HudCommandActions.cpp
        src/UI/HudCommandButtons.cpp
        src/UI/HudCommandPanel.cpp
        src/UI/HudContext.cpp
        src/UI/HudInfoPanelState.cpp
    )

    target_include_directories(HudCommandMetadataTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(HudCommandMetadataTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(HudCommandMetadataTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(HudCommandMetadataTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_CHK_STRING_ENCODING_TEST "Build CHK string encoding regression test" ON)
if(BUILD_CHK_STRING_ENCODING_TEST)
    add_executable(ChkStringEncodingTest
        src/Game/Regression/ChkStringEncodingTest.cpp
        src/Game/Map/ChkParser.cpp
    )

    target_include_directories(ChkStringEncodingTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(ChkStringEncodingTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(ChkStringEncodingTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(ChkStringEncodingTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_SMART_CLICK_COMMAND_TEST "Build smart-click command dispatch regression test" ON)
if(BUILD_SMART_CLICK_COMMAND_TEST)
    add_executable(SmartClickCommandTest
        src/Game/Regression/SmartClickCommandTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
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
        src/UI/HudCommandActions.cpp
        src/UI/HudCommandButtons.cpp
        src/UI/HudCommandPanel.cpp
        src/UI/HudContext.cpp
        src/UI/HudInfoPanelState.cpp
    )

    target_include_directories(SmartClickCommandTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(SmartClickCommandTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(SmartClickCommandTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(SmartClickCommandTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_SELECTION_INTERACTION_TEST "Build selection interaction regression test" ON)
if(BUILD_SELECTION_INTERACTION_TEST)
    add_executable(SelectionInteractionTest
        src/Game/Regression/SelectionInteractionTest.cpp
        src/Client/Selection.cpp
        src/Core/ECS/EntityManager.cpp
    )

    target_include_directories(SelectionInteractionTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(SelectionInteractionTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(SelectionInteractionTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(SelectionInteractionTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_SELECTION_FOOTPRINT_TEST "Build selection footprint regression test" ON)
if(BUILD_SELECTION_FOOTPRINT_TEST)
    add_executable(SelectionFootprintTest
        src/Game/Regression/SelectionFootprintTest.cpp
    )

    target_include_directories(SelectionFootprintTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(SelectionFootprintTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(SelectionFootprintTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(SelectionFootprintTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_MOUSE_COORDINATE_TEST "Build mouse coordinate regression test" ON)
if(BUILD_MOUSE_COORDINATE_TEST)
    add_executable(MouseCoordinateTest
        src/Game/Regression/MouseCoordinateTest.cpp
    )

    target_include_directories(MouseCoordinateTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(MouseCoordinateTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(MouseCoordinateTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(MouseCoordinateTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_CAMERA_EDGE_SCROLL_TEST "Build camera edge-scroll regression test" ON)
if(BUILD_CAMERA_EDGE_SCROLL_TEST)
    add_executable(CameraEdgeScrollTest
        src/Game/Regression/CameraEdgeScrollTest.cpp
        src/Client/CameraController.cpp
    )

    target_include_directories(CameraEdgeScrollTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(CameraEdgeScrollTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(CameraEdgeScrollTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(CameraEdgeScrollTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_MOVEMENT_COMMAND_TEST "Build movement command regression test" ON)
if(BUILD_MOVEMENT_COMMAND_TEST)
    add_executable(MovementCommandTest
        src/Game/Regression/MovementCommandTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
    )

    target_include_directories(MovementCommandTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(MovementCommandTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(MovementCommandTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(MovementCommandTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_NAVIGATION_PATHFINDING_TEST
    "Build minitile navigation pathfinding regression test" ON)
if(BUILD_NAVIGATION_PATHFINDING_TEST)
    add_executable(NavigationPathfindingTest
        src/Game/Regression/NavigationPathfindingTest.cpp)

    target_include_directories(NavigationPathfindingTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(NavigationPathfindingTest PRIVATE
            /W4 /utf-8 /permissive-)
        target_compile_definitions(NavigationPathfindingTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(NavigationPathfindingTest PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_NAVIGATION_DETERMINISM_TEST
    "Build fixed-tick navigation determinism regression test" ON)
if(BUILD_NAVIGATION_DETERMINISM_TEST)
    add_executable(NavigationDeterminismTest
        src/Game/Regression/NavigationDeterminismTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp)

    target_include_directories(NavigationDeterminismTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(NavigationDeterminismTest PRIVATE
            /W4 /utf-8 /permissive-)
        target_compile_definitions(NavigationDeterminismTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(NavigationDeterminismTest PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_NAVIGATION_STRESS_TEST
    "Build 800-unit navigation stress benchmark" ON)
if(BUILD_NAVIGATION_STRESS_TEST)
    add_executable(NavigationStressTest
        src/Game/Regression/NavigationStressTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp)

    target_include_directories(NavigationStressTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(NavigationStressTest PRIVATE
            /W4 /utf-8 /permissive-)
        target_compile_definitions(NavigationStressTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(NavigationStressTest PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_PRODUCTION_ECONOMY_TEST "Build production economy regression test" ON)
if(BUILD_PRODUCTION_ECONOMY_TEST)
    add_executable(ProductionEconomyTest
        src/Game/Regression/ProductionEconomyTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
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

    target_include_directories(ProductionEconomyTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(ProductionEconomyTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(ProductionEconomyTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(ProductionEconomyTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_ZERG_CREEP_REGRESSION_TEST "Build Zerg creep regression test" ON)
if(BUILD_ZERG_CREEP_REGRESSION_TEST)
    add_executable(ZergCreepRegressionTest
        src/Game/Regression/ZergCreepRegressionTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
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

    target_include_directories(ZergCreepRegressionTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(ZergCreepRegressionTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(ZergCreepRegressionTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(ZergCreepRegressionTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_CURSOR_TARGETING_TEST "Build cursor targeting regression test" ON)
if(BUILD_CURSOR_TARGETING_TEST)
    add_executable(CursorTargetingTest
        src/Game/Regression/CursorTargetingTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Game/Races/RaceDef.cpp
        src/Main/GameLoop/CursorTypes.cpp
    )

    target_include_directories(CursorTargetingTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(CursorTargetingTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(CursorTargetingTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(CursorTargetingTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_HARVEST_SYSTEM_COMMAND_TEST "Build harvest command regression test" ON)
if(BUILD_HARVEST_SYSTEM_COMMAND_TEST)
    add_executable(HarvestSystemCommandTest
        src/Game/Regression/HarvestSystemCommandTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Pathfinding/AStar.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/MovementSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/Races/RaceDef.cpp
    )

    target_include_directories(HarvestSystemCommandTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(HarvestSystemCommandTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(HarvestSystemCommandTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(HarvestSystemCommandTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_BUILDING_PLACEMENT_TEST "Build building placement regression test" ON)
if(BUILD_BUILDING_PLACEMENT_TEST)
    add_executable(BuildingPlacementTest
        src/Game/Regression/BuildingPlacementTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/Map/ChkParser.cpp
        src/Game/Map/Map.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/BuildingSystem.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
    )

    target_include_directories(BuildingPlacementTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(BuildingPlacementTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(BuildingPlacementTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(BuildingPlacementTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_MATCH_OUTCOME_TEST "Build match outcome regression test" ON)
if(BUILD_MATCH_OUTCOME_TEST)
    add_executable(MatchOutcomeTest
        src/Game/Regression/MatchOutcomeTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/FogOfWarSystem.cpp
        src/Game/Systems/UpgradeSystem.cpp
        src/Game/IScriptVM.cpp
        src/Game/Map/Map.cpp
        src/Game/Map/ChkParser.cpp
    )

    target_include_directories(MatchOutcomeTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(MatchOutcomeTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(MatchOutcomeTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(MatchOutcomeTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_RESOURCE_EXTRACTOR "Build resource extraction tool" OFF)
if(BUILD_RESOURCE_EXTRACTOR)
    add_executable(ResourceExtractor
        tests/ResourceExtractor/ResourceExtractor.cpp
        src/Resource/MpqArchive.cpp
        src/Core/Utils/Logger.cpp
    )

    target_include_directories(ResourceExtractor PRIVATE ${CMAKE_SOURCE_DIR}/src)

    target_link_libraries(ResourceExtractor PRIVATE
        ${StormLib_TARGET}
    )

    target_compile_definitions(ResourceExtractor PRIVATE
        $<$<BOOL:${HAS_STORMLIB}>:HAS_STORMLIB>
        $<$<BOOL:${HAS_STORMLIB}>:STORMLIB_NO_AUTO_LINK>
        $<$<BOOL:${HAS_STORMLIB}>:__STORMLIB_NO_STATIC_LINK__>
    )

    if(MSVC)
        target_compile_options(ResourceExtractor PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(ResourceExtractor PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(ResourceExtractor PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_OGG_VORBIS_DECODE_TEST "Build Ogg/Vorbis decode regression test" ON)
if(BUILD_OGG_VORBIS_DECODE_TEST)
    add_executable(OggVorbisDecodeTest
        src/Game/Regression/OggVorbisDecodeTest.cpp
    )

    target_include_directories(OggVorbisDecodeTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(OggVorbisDecodeTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(OggVorbisDecodeTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(OggVorbisDecodeTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_TRANSPORT_SYSTEM_TEST "Build transport system regression test" ON)
if(BUILD_TRANSPORT_SYSTEM_TEST)
    add_executable(TransportSystemTest
        src/Game/Regression/TransportSystemTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
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

    target_include_directories(TransportSystemTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    if(MSVC)
        target_compile_options(TransportSystemTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(TransportSystemTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(TransportSystemTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_MANUAL_WEBM_VIDEO_DECODE_TEST "Build manual WebM/VPx decode regression targets" OFF)
if(BUILD_MANUAL_WEBM_VIDEO_DECODE_TEST AND HAS_LIBVPX)
    set(_WEBM_VIDEO_DECODE_TEST_SRC "${CMAKE_SOURCE_DIR}/src/Game/Regression/WebmVideoDecodeTest.cpp")
    set(_WEBM_ASSET_COVERAGE_TEST_SRC "${CMAKE_SOURCE_DIR}/src/Game/Regression/WebmAssetCoverageTest.cpp")
    if(EXISTS "${_WEBM_VIDEO_DECODE_TEST_SRC}" AND EXISTS "${_WEBM_ASSET_COVERAGE_TEST_SRC}")
        add_executable(WebmVideoDecodeTest
            ${_WEBM_VIDEO_DECODE_TEST_SRC}
            src/Resource/PortraitVideoDecoder.cpp
        )

        target_include_directories(WebmVideoDecodeTest PRIVATE
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/src)
        target_compile_definitions(WebmVideoDecodeTest PRIVATE HAS_LIBVPX)
        target_link_libraries(WebmVideoDecodeTest PRIVATE ${LIBVPX_TARGET})

        if(MSVC)
            target_compile_options(WebmVideoDecodeTest PRIVATE /W4 /utf-8 /permissive-)
            target_compile_definitions(WebmVideoDecodeTest PRIVATE
                _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
        else()
            target_compile_options(WebmVideoDecodeTest PRIVATE -Wall -Wextra -Wpedantic)
        endif()

        add_executable(WebmAssetCoverageTest
            ${_WEBM_ASSET_COVERAGE_TEST_SRC}
            src/Resource/PortraitVideoDecoder.cpp
        )

        target_include_directories(WebmAssetCoverageTest PRIVATE
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/src)
        target_compile_definitions(WebmAssetCoverageTest PRIVATE HAS_LIBVPX)
        target_link_libraries(WebmAssetCoverageTest PRIVATE ${LIBVPX_TARGET})

        if(MSVC)
            target_compile_options(WebmAssetCoverageTest PRIVATE /W4 /utf-8 /permissive-)
            target_compile_definitions(WebmAssetCoverageTest PRIVATE
                _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
        else()
            target_compile_options(WebmAssetCoverageTest PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    else()
        message(STATUS "  WebM tests: skipped (regression source file missing)")
    endif()
endif()

option(BUILD_TRIGGER_RUNTIME_TEST "Build trigger parser/runtime regression test" ON)
if(BUILD_TRIGGER_RUNTIME_TEST)
    add_executable(TriggerRuntimeTest
        tests/TriggerRuntimeTest/TriggerRuntimeTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/Campaign/CampaignController.cpp
        src/Game/Trigger/TriggerVM.cpp
        src/Game/IScriptVM.cpp
        src/Game/Races/RaceDef.cpp
        src/Game/Systems/HarvestSystem.cpp
        src/Game/Systems/AnimationSystem.cpp
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
        src/Resource/GameDetector.cpp
    )

    target_include_directories(TriggerRuntimeTest PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)

    target_link_libraries(TriggerRuntimeTest PRIVATE
        ${StormLib_TARGET}
        ${CascLib_TARGET}
    )

    target_compile_definitions(TriggerRuntimeTest PRIVATE
        $<$<BOOL:${HAS_STORMLIB}>:HAS_STORMLIB>
        $<$<BOOL:${HAS_STORMLIB}>:STORMLIB_NO_AUTO_LINK>
        $<$<BOOL:${HAS_STORMLIB}>:__STORMLIB_NO_STATIC_LINK__>
        $<$<BOOL:${HAS_CASCLIB}>:HAS_CASCLIB>
    )

    if(MSVC)
        target_compile_options(TriggerRuntimeTest PRIVATE /W4 /utf-8 /permissive-)
        target_compile_definitions(TriggerRuntimeTest PRIVATE
            _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(TriggerRuntimeTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(BUILD_CAMPAIGN_CATALOG_RUNTIME_TEST "Build all-campaign catalog runtime regression test" ON)
if(BUILD_CAMPAIGN_CATALOG_RUNTIME_TEST)
    add_executable(CampaignCatalogRuntimeTest
        src/Game/Regression/CampaignCatalogRuntimeTest.cpp
        src/Core/ECS/EntityManager.cpp
        src/Core/ECS/SystemManager.cpp
        src/Core/Utils/Logger.cpp
        src/Game/Campaign/CampaignController.cpp
        src/Game/Campaign/CampaignMissionCatalog.cpp
        src/Game/Trigger/TriggerVM.cpp
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
        src/Resource/GameDetector.cpp
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

foreach(_StarCraftRegressionTest IN ITEMS
    IScriptAngelScriptExportTest
    IScriptAngelScriptCompileTest
    AnimationStateTest
    AnimationRuntimeDirectionTest
    MovementCommandTest
    NavigationPathfindingTest
    NavigationDeterminismTest
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
    TriggerRuntimeTest
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
        src/Resource/UIAssetImageHelpers.cpp
        src/Resource/PortraitVideoDecoder.cpp
        src/Resource/PcxToGpu.cpp
        src/Resource/ResourceCache.cpp
        src/Resource/ResourceCacheUI.cpp
        src/Resource/ResourceCacheUIFrames.cpp
        src/Resource/ResourceCacheUIConsole.cpp
        src/Resource/ResourceCacheUIAssets.cpp
        src/Resource/ResourceCacheInitialize.cpp
        src/Resource/ResourceCacheData.cpp
        src/Resource/ResourceCacheSprites.cpp
        src/Resource/ResourceCacheTilesetData.cpp
        src/Resource/ResourceCacheTilesetImages.cpp
        src/Resource/ResourceCacheUIAccess.cpp
        src/Resource/ResourceCacheViews.cpp
        src/Resource/ResourceCachePortrait.cpp
        src/Resource/ResourceCacheLifecycle.cpp
        src/Resource/ResourceCacheSources.cpp
        src/Resource/IArchiveReader.cpp
        src/Resource/MpqArchive.cpp
        src/Resource/CascArchive.cpp
        src/Resource/FileSystemArchive.cpp
        src/Resource/GameDetector.cpp
        src/Resource/GameDataLoader.cpp
        src/Resource/GrpDecoder.cpp
        src/Resource/Cv5Decoder.cpp
        src/Resource/Vx4Decoder.cpp
        src/Resource/Vf4Decoder.cpp
        src/Resource/DatDecoder.cpp
        src/Resource/PalDecoder.cpp
        src/Resource/TblDecoder.cpp
        src/Game/Map/ChkParser.cpp
        src/Client/Entity.cpp
        src/Client/MapLoader.cpp
        src/Client/ClientRuntime.cpp
        src/Client/WorldEffects.cpp
        src/Game/IScriptVM.cpp
        src/Client/TilesetRenderer.cpp
        src/Client/EntityRenderer.cpp
        src/Client/CameraController.cpp
        src/Client/Selection.cpp
        src/Client/SelectionOverlayPresentation.cpp
        src/Client/ProjectileSystem.cpp
        "src/UI/GlueCampaignData.cpp"
        "src/UI/Hud.cpp"
        "src/UI/HudCommandActions.cpp"
        "src/UI/HudCommandButtons.cpp"
        "src/UI/HudCommandPanel.cpp"
        "src/UI/HudCommandPanelRender.cpp"
        "src/UI/HudConsole.cpp"
        "src/UI/HudContext.cpp"
        "src/UI/HudInfoPanel.cpp"
        "src/UI/HudInfoPanelFrame.cpp"
        "src/UI/HudInfoPanelPortrait.cpp"
        "src/UI/HudInfoPanelScv.cpp"
        "src/UI/HudInfoPanelStandard.cpp"
        "src/UI/HudInfoPanelState.cpp"
        "src/UI/HudInput.cpp"
        "src/UI/HudLayout.cpp"
        "src/UI/HudMinimap.cpp"
        "src/UI/HudResources.cpp"
        "src/Main/AppRuntime.cpp"
        "src/Main/CampaignAudio.cpp"
        "src/Main/CampaignPortrait.cpp"
        "src/Main/CampaignPresentation.cpp"
        "src/Main/CampaignText.cpp"
        "src/Main/GameMenuPresentation.cpp"
        "src/Main/GameControllerAppApi.cpp"
        "src/Main/GameControllerFrame.cpp"
        "src/Main/GameControllerLifecycle.cpp"
        "src/Main/GameControllerSessionApi.cpp"
        "src/Main/GameControllerStartup.cpp"
        "src/Main/GameGuiPresentation.cpp"
        "src/Main/GameGuiBootstrap.cpp"
        "src/Main/GameLoop/CursorTypes.cpp"
        "src/Main/GameLoop/CursorManager.cpp"
        "src/Main/GameLoop/EcsSync.cpp"
        "src/Main/GameLoop/GameSaveState.cpp"
        "src/Main/GameLoop/HudResourceSync.cpp"
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
        src/Game/Campaign/CampaignProgressStore.cpp
        src/Game/Trigger/TriggerVM.cpp
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
            "src/UI/GuiHost.cpp"
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
