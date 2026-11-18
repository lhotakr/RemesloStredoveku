cmake_minimum_required(VERSION 3.20)
project(RemesloStredoveku)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SKIP_RPATH TRUE)

option(ATLAS_VIEWER "Build/run atlas viewer instead of the game" OFF)

find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_IMAGE QUIET IMPORTED_TARGET SDL2_image)
pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)

if(NOT SDL2_IMAGE_FOUND)
    find_library(SDL2_IMAGE_LIBRARY
            NAMES SDL2_image SDL2_image-2.0
            PATHS /run/host/usr/lib64 /run/host/usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu
            NO_DEFAULT_PATH
    )
    if(NOT SDL2_IMAGE_LIBRARY)
        find_file(SDL2_IMAGE_LIBRARY
                NAMES libSDL2_image-2.0.so.0 libSDL2_image.so
                PATHS /run/host/usr/lib64 /run/host/usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu
                NO_DEFAULT_PATH
        )
    endif()

    if(NOT SDL2_IMAGE_LIBRARY)
        message(FATAL_ERROR "SDL2_image was not found. Install SDL2_image development files or make libSDL2_image available.")
    endif()

    add_library(PkgConfig::SDL2_IMAGE UNKNOWN IMPORTED)
    set_target_properties(PkgConfig::SDL2_IMAGE PROPERTIES
            IMPORTED_LOCATION "${SDL2_IMAGE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/sdl2_image_stub"
            INTERFACE_LINK_LIBRARIES PkgConfig::SDL2
    )
endif()

# Fix for missing rav1e include directory - filter out non-existent paths and add host includes
get_target_property(SDL2_IMAGE_INCLUDES PkgConfig::SDL2_IMAGE INTERFACE_INCLUDE_DIRECTORIES)
set(SDL2_IMAGE_INCLUDES_FILTERED "")
foreach(INCLUDE_DIR ${SDL2_IMAGE_INCLUDES})
    if(EXISTS "${INCLUDE_DIR}")
        list(APPEND SDL2_IMAGE_INCLUDES_FILTERED "${INCLUDE_DIR}")
    endif()
endforeach()
# Add host SDL2 includes for Flatpak environment
if(EXISTS "/run/host/usr/include/SDL2")
    list(APPEND SDL2_IMAGE_INCLUDES_FILTERED "/run/host/usr/include/SDL2")
endif()
set_target_properties(PkgConfig::SDL2_IMAGE PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_IMAGE_INCLUDES_FILTERED}"
)

# Also fix SDL2 includes
get_target_property(SDL2_INCLUDES PkgConfig::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
set(SDL2_INCLUDES_FILTERED "")
foreach(INCLUDE_DIR ${SDL2_INCLUDES})
    if(EXISTS "${INCLUDE_DIR}")
        list(APPEND SDL2_INCLUDES_FILTERED "${INCLUDE_DIR}")
    endif()
endforeach()
if(EXISTS "/run/host/usr/include/SDL2")
    list(APPEND SDL2_INCLUDES_FILTERED "/run/host/usr/include/SDL2")
endif()
set_target_properties(PkgConfig::SDL2 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDES_FILTERED}"
)

# Fix library paths for Flatpak environment
if(EXISTS "/run/host/usr/lib64")
    get_target_property(SDL2_IMAGE_LIBS PkgConfig::SDL2_IMAGE INTERFACE_LINK_LIBRARIES)
    set(SDL2_IMAGE_LIBS_FILTERED "")
    foreach(LIB ${SDL2_IMAGE_LIBS})
        if(EXISTS "${LIB}")
            list(APPEND SDL2_IMAGE_LIBS_FILTERED "${LIB}")
        else()
            # Try to find it on host
            get_filename_component(LIB_NAME "${LIB}" NAME)
            if(EXISTS "/run/host/usr/lib64/${LIB_NAME}")
                list(APPEND SDL2_IMAGE_LIBS_FILTERED "/run/host/usr/lib64/${LIB_NAME}")
            else()
                list(APPEND SDL2_IMAGE_LIBS_FILTERED "${LIB}")
            endif()
        endif()
    endforeach()
    set_target_properties(PkgConfig::SDL2_IMAGE PROPERTIES
        INTERFACE_LINK_LIBRARIES "${SDL2_IMAGE_LIBS_FILTERED}"
    )

    # Same for SDL2_MIXER
    get_target_property(SDL2_MIXER_LIBS PkgConfig::SDL2_MIXER INTERFACE_LINK_LIBRARIES)
    set(SDL2_MIXER_LIBS_FILTERED "")
    foreach(LIB ${SDL2_MIXER_LIBS})
        if(EXISTS "${LIB}")
            list(APPEND SDL2_MIXER_LIBS_FILTERED "${LIB}")
        else()
            get_filename_component(LIB_NAME "${LIB}" NAME)
            if(EXISTS "/run/host/usr/lib64/${LIB_NAME}")
                list(APPEND SDL2_MIXER_LIBS_FILTERED "/run/host/usr/lib64/${LIB_NAME}")
            else()
                list(APPEND SDL2_MIXER_LIBS_FILTERED "${LIB}")
            endif()
        endif()
    endforeach()
    set_target_properties(PkgConfig::SDL2_MIXER PROPERTIES
        INTERFACE_LINK_LIBRARIES "${SDL2_MIXER_LIBS_FILTERED}"
    )

    # Same for SDL2
    get_target_property(SDL2_LIBS PkgConfig::SDL2 INTERFACE_LINK_LIBRARIES)
    set(SDL2_LIBS_FILTERED "")
    foreach(LIB ${SDL2_LIBS})
        if(EXISTS "${LIB}")
            list(APPEND SDL2_LIBS_FILTERED "${LIB}")
        else()
            get_filename_component(LIB_NAME "${LIB}" NAME)
            if(EXISTS "/run/host/usr/lib64/${LIB_NAME}")
                list(APPEND SDL2_LIBS_FILTERED "/run/host/usr/lib64/${LIB_NAME}")
            else()
                list(APPEND SDL2_LIBS_FILTERED "${LIB}")
            endif()
        endif()
    endforeach()
    set_target_properties(PkgConfig::SDL2 PROPERTIES
        INTERFACE_LINK_LIBRARIES "${SDL2_LIBS_FILTERED}"
    )

endif()

add_library(RemesloMedievalUI STATIC
        src/ui/MedievalUI.h
        src/ui/MedievalUI.cpp
        src/ui/DialoguePanel.cpp
        src/ui/DocumentReader.cpp
)
target_include_directories(RemesloMedievalUI PUBLIC src third_party/imgui PRIVATE third_party/nlohmann)
target_link_libraries(RemesloMedievalUI PUBLIC PkgConfig::SDL2 PkgConfig::SDL2_IMAGE)
target_compile_options(RemesloMedievalUI PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

add_executable(RemesloStredoveku
        # main
        src/main.cpp

        # game
        src/game/Game.cpp
        src/game/Game.h
        src/game/ItemEditor.h
        src/game/ItemEditor.cpp
        src/game/ItemCatalog.h
        src/game/ItemCatalog.cpp
        src/game/EditorDocument.cpp
        src/game/NpcCatalog.cpp
        src/game/NpcEditor.cpp
        src/game/BuildInteriorEngine.cpp
        src/game/InteriorEditorGeometry.cpp
        src/game/InteriorEditorAssets.cpp
        src/game/InventorySpriteCatalog.cpp
        src/game/BuildInteriorEngine.h
        src/game/CinematicScene.cpp
        src/game/CinematicScene2D.cpp
        src/game/CinematicScene.h
        src/game/InteriorInteractionEvent.h
        src/game/Campaign.cpp
        src/game/CampaignConsole.cpp
        src/game/CampaignLighting.cpp
        src/game/CampaignCollision.cpp
        src/game/CampaignRender.cpp
        src/game/CampaignSharedRuntime.cpp
        src/game/CampaignSmithing.cpp
        src/game/CampaignUpdate.cpp
        src/game/CampaignInit.cpp
        src/game/CampaignSequence.cpp
        src/game/CampaignFogOfWar.cpp
        src/game/CampaignDialog.cpp
        src/game/CampaignDialogUI.cpp
        src/game/CampaignMenusUI.cpp
        src/game/CampaignForageUI.cpp
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
        src/game/sequence/SequenceActions.h
        src/game/sequence/SequenceActions.cpp
        src/game/sequence/SequenceDefinition.h
        src/game/sequence/SequenceDefinition.cpp
        src/game/sequence/SequenceEditing.cpp
        src/game/sequence/SequenceMarkers.h
        src/game/sequence/SequenceMarkers.cpp
        src/game/sequence/SequenceRuntime.h
        src/game/sequence/SequenceRuntime.cpp
        src/game/sequence/SequenceTimeline.cpp
        src/game/sequence/SequenceAudio.cpp
        src/game/sequence/SequenceManager.h
        src/game/sequence/SequenceManager.cpp
        src/game/smithing/NailSmithing.h
        src/game/smithing/NailSmithing.cpp
        src/game/smithing/SmithingAnimationManifest.h
        src/game/smithing/SmithingAnimationManifest.cpp
        src/game/smithing/NailForgingState.h
        src/game/smithing/NailForgingController.h
        src/game/smithing/NailForgingController.cpp
        src/game/smithing/SmithingNailAudioController.h
        src/game/smithing/SmithingNailAudioController.cpp

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
        src/game/EditorSequences.cpp
        src/game/EditorSequenceMarkers.cpp
        src/game/EditorSequenceEntities.cpp
        src/game/EditorSequenceScene2D.cpp
        src/game/EditorSequenceTimeline.cpp
        src/game/EditorSequenceMotion.cpp
        src/game/EditorSequenceAudio.cpp
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
        third_party/nlohmann
)

if(EXISTS "/run/host/usr/include/SDL2")
    target_include_directories(RemesloStredoveku PRIVATE /run/host/usr/include/SDL2)
endif()

target_link_libraries(RemesloStredoveku PRIVATE
        RemesloMedievalUI
        PkgConfig::SDL2
        PkgConfig::SDL2_IMAGE
        PkgConfig::SDL2_MIXER
)

if(NOT SDL2_IMAGE_FOUND)
    target_link_options(RemesloStredoveku PRIVATE -Wl,--allow-shlib-undefined)
endif()

target_compile_definitions(RemesloStredoveku PRIVATE
        SDL_MAIN_HANDLED
        REMESLO_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

if(ATLAS_VIEWER)
    target_compile_definitions(RemesloStredoveku PRIVATE ATLAS_VIEWER)
endif()

if(MSVC)
    target_compile_options(RemesloStredoveku PRIVATE /utf-8)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(RemesloStredoveku PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
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

add_executable(RemesloSmithingTests
        src/tests/NailSmithingTests.cpp
        src/game/smithing/NailSmithing.h
        src/game/smithing/NailSmithing.cpp
        src/game/smithing/SmithingAnimationManifest.h
        src/game/smithing/SmithingAnimationManifest.cpp
        src/game/smithing/NailForgingState.h
        src/game/smithing/NailForgingController.h
        src/game/smithing/NailForgingController.cpp
)

target_include_directories(RemesloSmithingTests PRIVATE
        src
        third_party/nlohmann
)

target_link_libraries(RemesloSmithingTests PRIVATE
)

target_compile_definitions(RemesloSmithingTests PRIVATE
        REMESLO_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

enable_testing()
add_test(NAME RemesloSmithingTests COMMAND RemesloSmithingTests)

add_executable(RemesloSequenceTests
        src/tests/SequenceTests.cpp
        src/audio/AudioManager.cpp
        src/game/sequence/SequenceDefinition.cpp
        src/game/sequence/SequenceEditing.cpp
        src/game/sequence/SequenceActions.cpp
        src/game/sequence/SequenceRuntime.cpp
        src/game/sequence/SequenceMarkers.cpp
        src/game/sequence/SequenceTimeline.cpp
        src/game/sequence/SequenceAudio.cpp
        src/game/sequence/SequenceManager.cpp
        src/JsonUtils.cpp
)
target_include_directories(RemesloSequenceTests PRIVATE src third_party/nlohmann)
target_link_libraries(RemesloSequenceTests PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_MIXER)
target_compile_definitions(RemesloSequenceTests PRIVATE SDL_MAIN_HANDLED REMESLO_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME RemesloSequenceTests COMMAND RemesloSequenceTests)
add_test(NAME RemesloSequenceAudioTests COMMAND RemesloSequenceTests --audio)

add_executable(RemesloInteriorEditorTests
        src/tests/InteriorEditorTests.cpp
        src/CharacterManager.cpp
        src/JsonUtils.cpp
        src/game/CinematicScene.cpp
        src/game/CinematicScene2D.cpp
        src/game/sequence/SequenceMarkers.cpp
        src/game/sequence/SequenceDefinition.cpp
        src/game/sequence/SequenceEditing.cpp
        src/game/sequence/SequenceRuntime.cpp
        src/game/sequence/SequenceTimeline.cpp
        src/terrain/ObjectCatalog.cpp
        src/game/BuildInteriorEngine.cpp
        src/game/InteriorEditorGeometry.cpp
        src/game/InteriorEditorAssets.cpp
        src/game/InventorySpriteCatalog.cpp
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
)
target_include_directories(RemesloInteriorEditorTests PRIVATE src third_party/nlohmann third_party/imgui)
target_link_libraries(RemesloInteriorEditorTests PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_IMAGE)
target_compile_definitions(RemesloInteriorEditorTests PRIVATE SDL_MAIN_HANDLED REMESLO_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
if(MSVC)
    target_compile_options(RemesloInteriorEditorTests PRIVATE /utf-8)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(RemesloInteriorEditorTests PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()
add_test(NAME RemesloInteriorEditorTests COMMAND RemesloInteriorEditorTests)
set_tests_properties(RemesloInteriorEditorTests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(RemesloMedievalUITests
        src/tests/MedievalUITests.cpp
        src/game/DialogManager.cpp
        src/JsonUtils.cpp
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(RemesloMedievalUITests PRIVATE third_party/nlohmann)
target_link_libraries(RemesloMedievalUITests PRIVATE RemesloMedievalUI)
target_compile_definitions(RemesloMedievalUITests PRIVATE SDL_MAIN_HANDLED REMESLO_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_options(RemesloMedievalUITests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME RemesloMedievalUITests COMMAND RemesloMedievalUITests)
set_tests_properties(RemesloMedievalUITests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(RemesloItemCatalogTests
        src/tests/ItemCatalogTests.cpp
        src/game/ItemCatalog.cpp
        src/game/EditorDocument.cpp
        src/JsonUtils.cpp
)
target_include_directories(RemesloItemCatalogTests PRIVATE src third_party/nlohmann)
target_compile_definitions(RemesloItemCatalogTests PRIVATE REMESLO_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_options(RemesloItemCatalogTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME RemesloItemCatalogTests COMMAND RemesloItemCatalogTests)

add_executable(RemesloNpcCatalogTests
        src/tests/NpcCatalogTests.cpp
        src/game/NpcCatalog.cpp
        src/game/EditorDocument.cpp
        src/NpcDefinition.cpp
        src/CharacterManager.cpp
        src/Player.cpp
        src/game/NpcManager.cpp
        src/audio/AudioManager.cpp
        src/JsonUtils.cpp
)
target_include_directories(RemesloNpcCatalogTests PRIVATE src third_party/nlohmann)
target_link_libraries(RemesloNpcCatalogTests PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_IMAGE PkgConfig::SDL2_MIXER)
target_compile_definitions(RemesloNpcCatalogTests PRIVATE SDL_MAIN_HANDLED REMESLO_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_options(RemesloNpcCatalogTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME RemesloNpcCatalogTests COMMAND RemesloNpcCatalogTests)
