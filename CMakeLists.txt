cmake_minimum_required(VERSION 3.20)
project(RemesloStredoveku)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ATLAS_VIEWER "Build/run atlas viewer instead of the game" OFF)

find_package(PkgConfig REQUIRED)
find_package(nlohmann_json REQUIRED)

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)

add_executable(RemesloStredoveku
        # main
        src/main.cpp

        # game
        src/game/Game.cpp
        src/game/Game.h
        src/game/BuildInteriorEngine.cpp
        src/game/BuildInteriorEngine.h
        src/game/Campaign.cpp
        src/game/CampaignConsole.cpp
        src/game/CampaignLighting.cpp
        src/game/CampaignCollision.cpp
        src/game/CampaignRender.cpp
        src/game/CampaignUpdate.cpp
        src/game/CampaignInit.cpp
        src/game/CampaignFogOfWar.cpp
        src/game/CampaignDialog.cpp
        src/game/CampaignQuests.cpp
        src/game/CampaignHudAssets.cpp
        src/game/Campaign.h
        src/game/Editor.cpp
        src/game/Editor.h
        src/game/NpcManager.cpp
        src/game/NpcManager.h
        src/game/GameTime.h
        src/game/GameTime.cpp
        src/game/LiturgicalCalendar.h
        src/game/LiturgicalCalendar.cpp
        src/game/SunCycle.h
        src/game/SunCycle.cpp
        src/game/MoonCycle.h
        src/game/MoonCycle.cpp
        src/game/DialogManager.h
        src/game/DialogManager.cpp
        src/game/WeatherTypes.h
        src/game/WeatherSystem.h
        src/game/WeatherSystem.cpp

        # player / characters
        src/Player.cpp
        src/Player.h
        src/PlayerInventory.h
        src/CharacterManager.cpp
        src/CharacterManager.h

        # items / npc defs
        src/ItemTypes.h
        src/ItemDef.h
        src/NpcDefinition.h
        src/NpcDefinition.cpp

        # terrain
        src/terrain/TileMap.cpp
        src/terrain/TileMap.h
        src/terrain/TerrainTileset.cpp
        src/terrain/TerrainTileset.h
        src/terrain/TerrainRenderer.cpp
        src/terrain/TerrainRenderer.h
        src/terrain/ObjectCatalog.cpp
        src/terrain/ObjectCatalog.h

        # audio
        src/audio/AudioManager.cpp
        src/audio/AudioManager.h

        # utils
        src/Utf8.h
        src/PathUtils.h
        src/JsonUtils.h
        src/JsonUtils.cpp
        src/ImGuiUtils.h
        src/ImGuiUtils.cpp

        # imgui
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_sdl2.cpp
        third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp
        src/game/ForageTypes.h
        src/game/ForageDatabase.h
        src/game/ForageDatabase.cpp
        src/game/ForageSystem.h
        src/game/ForageSystem.cpp
        src/game/EditorTypes.h
        src/game/EditorCore.cpp
        src/game/EditorUndo.cpp
        src/game/EditorZones.cpp
        src/game/EditorNpc.cpp
        src/game/EditorDialogs.cpp
        src/game/EditorSchedules.cpp
        src/game/EditorForage.cpp
        src/game/EditorHelpers.cpp
        src/game/EditorHelpers.h
        src/game/EditorQuests.cpp
        src/game/ObjectEditor.cpp
        src/game/ObjectEditor.h
        src/game/interior/LocationRouter.h

)

target_include_directories(RemesloStredoveku PRIVATE
        src
        third_party/imgui
        third_party/imgui/backends
)

target_link_libraries(RemesloStredoveku PRIVATE
        PkgConfig::SDL2
        PkgConfig::SDL2_IMAGE
        PkgConfig::SDL2_MIXER
        nlohmann_json::nlohmann_json
)

target_compile_definitions(RemesloStredoveku PRIVATE
        SDL_MAIN_HANDLED
        REMESLO_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

if(ATLAS_VIEWER)
    target_compile_definitions(RemesloStredoveku PRIVATE ATLAS_VIEWER)
endif()

if(MSVC)
    target_compile_options(RemesloStredoveku PRIVATE /utf-8)
endif()

# kop�rov�n� runtime dat do slo�ky vedle RemesloStredoveku.exe
# D�ky tomu hra/editor najde assets/... i data/... i p�i spu�t�n� z build adres��e.
add_custom_command(TARGET RemesloStredoveku POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:RemesloStredoveku>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:RemesloStredoveku>/assets"

        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:RemesloStredoveku>/data"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/data"
        "$<TARGET_FILE_DIR:RemesloStredoveku>/data"
)

# Windows + MSYS2 runtime DLLs
if(WIN32)
    file(GLOB MSYS2_RUNTIME_DLLS "C:/msys64/mingw64/bin/*.dll")

    foreach(DLL ${MSYS2_RUNTIME_DLLS})
        add_custom_command(TARGET RemesloStredoveku POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL}"
                $<TARGET_FILE_DIR:RemesloStredoveku>
        )
    endforeach()
endif()