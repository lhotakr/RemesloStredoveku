#define NOMINMAX
#include "BuildInteriorEngine.h"

#include "PlayerStats.h"
#include "Utf8.h"
#include "interior/InteriorMapLoader.h"
#include "interior/InteriorLegacyAdapter.h"
#include "../PathUtils.h"

#include <SDL_image.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>
#include "imgui.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kPlayerRadius = 0.18;
constexpr double kPlayerBodyHeight = 0.90;
constexpr double kMinimumHeadroom = 0.06;
constexpr double kInteriorUnitsPerCampaignPixel = 2.55 / 90.0;
constexpr double kMaxStepUp = 0.45;
// Descending a normal tread must not require a jump.  The old 0.22 m limit
// was just below the 0.23 m rise generated for the rock-room staircase.
constexpr double kMaxStepDown = 0.45;
constexpr double kJumpVelocity = 4.25;
constexpr double kGravity = 10.8;
constexpr double kAirborneStepClearance = 0.12;
constexpr double kHuge = 1.0e30;
constexpr double kPortalFloorContinuityEpsilon = 0.025;
constexpr double kPortalCeilingContinuityEpsilon = 0.025;
constexpr int kVoxelMaxGrid = 28;
constexpr std::uint8_t kVoxelLeft = 1u << 0u;
constexpr std::uint8_t kVoxelRight = 1u << 1u;
constexpr std::uint8_t kVoxelTop = 1u << 2u;
constexpr std::uint8_t kVoxelBottom = 1u << 3u;
constexpr const char* kPolygonBuildStamp =
    "POLYGON_V26_UINT16_TEXTURE_KEYS_2026_07_31";

fs::path ProjectRootPath()
{
#ifdef REMESLO_PROJECT_ROOT
    return fs::path(REMESLO_PROJECT_ROOT);
#else
    return fs::current_path();
#endif
}

double wrapAngle(double value)
{
    while (value < -kPi) value += 2.0 * kPi;
    while (value >  kPi) value -= 2.0 * kPi;
    return value;
}

bool isPathLike(const std::string& value)
{
    return value.find('/') != std::string::npos ||
           value.find('\\') != std::string::npos ||
           value.ends_with(".json");
}

bool parseCampaignLocation(const std::string& value, std::string& outMap)
{
    constexpr const char* prefix = "campaign:";
    if (!value.starts_with(prefix))
        return false;

    outMap = value.substr(std::strlen(prefix));
    return !outMap.empty();
}

bool isEmptyCell(std::uint16_t value)
{
    return value == 0;
}

std::uint16_t textureKeyFromJson(const json& source, const char* numericName,
                                 const char* legacyName, std::uint16_t fallback)
{
    if (source.contains(numericName) && source[numericName].is_number_unsigned())
        return source[numericName].get<std::uint16_t>();
    if (source.contains(numericName) && source[numericName].is_number_integer())
        return static_cast<std::uint16_t>(std::clamp(source[numericName].get<int>(), 0, 65535));
    if (source.contains(legacyName))
    {
        const json& value = source[legacyName];
        if (value.is_number_integer())
            return static_cast<std::uint16_t>(std::clamp(value.get<int>(), 0, 65535));
        if (value.is_string())
        {
            const std::string text = value.get<std::string>();
            if (!text.empty()) return static_cast<std::uint8_t>(text.front());
        }
    }
    return fallback;
}

int jsonColor(const json& value, int index, int fallback)
{
    if (!value.is_array() || index < 0 || index >= static_cast<int>(value.size()))
        return fallback;
    return std::clamp(value[index].get<int>(), 0, 255);
}

ImU32 imguiColor(int r, int g, int b, int a = 255)
{
    return ImGui::GetColorU32(ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f));
}

std::uint32_t argb(int r, int g, int b, int a = 255)
{
    return (static_cast<std::uint32_t>(std::clamp(a, 0, 255)) << 24u) |
           (static_cast<std::uint32_t>(std::clamp(r, 0, 255)) << 16u) |
           (static_cast<std::uint32_t>(std::clamp(g, 0, 255)) << 8u) |
            static_cast<std::uint32_t>(std::clamp(b, 0, 255));
}

std::uint32_t modulate(std::uint32_t color, double factor)
{
    factor = std::clamp(factor, 0.0, 1.35);
    const int a = static_cast<int>((color >> 24u) & 0xffu);
    const int r = static_cast<int>((color >> 16u) & 0xffu);
    const int g = static_cast<int>((color >> 8u) & 0xffu);
    const int b = static_cast<int>(color & 0xffu);
    return argb(static_cast<int>(r * factor), static_cast<int>(g * factor), static_cast<int>(b * factor), a);
}

std::uint32_t alphaBlend(std::uint32_t dst, std::uint32_t src)
{
    const int sa = static_cast<int>((src >> 24u) & 0xffu);
    if (sa <= 0) return dst;
    if (sa >= 255) return src;

    const int sr = static_cast<int>((src >> 16u) & 0xffu);
    const int sg = static_cast<int>((src >> 8u) & 0xffu);
    const int sb = static_cast<int>(src & 0xffu);
    const int dr = static_cast<int>((dst >> 16u) & 0xffu);
    const int dg = static_cast<int>((dst >> 8u) & 0xffu);
    const int db = static_cast<int>(dst & 0xffu);
    const int inv = 255 - sa;
    return argb((sr * sa + dr * inv) / 255,
                (sg * sa + dg * inv) / 255,
                (sb * sa + db * inv) / 255,
                255);
}

char firstCharOr(const std::string& value, char fallback)
{
    return value.empty() ? fallback : value.front();
}

int sectorCode(char symbol)
{
    return static_cast<int>(static_cast<unsigned char>(symbol));
}

std::vector<char> sectorSymbolOrder()
{
    std::vector<char> symbols;
    symbols.reserve(255);
    std::vector<bool> used(256, false);
    auto addCode = [&](int code)
    {
        if (code < 1 || code > 255 || used[static_cast<std::size_t>(code)])
            return;
        used[static_cast<std::size_t>(code)] = true;
        symbols.push_back(static_cast<char>(static_cast<unsigned char>(code)));
    };

    const std::string legacyPool =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (unsigned char c : legacyPool)
        addCode(static_cast<int>(c));
    for (int code = 33; code <= 126; ++code)
        addCode(code);
    for (int code = 1; code <= 255; ++code)
        addCode(code);
    return symbols;
}

std::string sectorKeyString(char symbol)
{
    const int code = sectorCode(symbol);
    if (code >= 33 && code <= 126 && symbol != '"' && symbol != '\\')
        return std::string(1, symbol);
    return "#" + std::to_string(code);
}

char sectorSymbolFromKey(const std::string& key, char fallback = 'A')
{
    if (key.size() == 1)
        return key.front();
    std::string numberText;
    if (!key.empty() && key.front() == '#')
        numberText = key.substr(1);
    else if (key.rfind("code:", 0) == 0)
        numberText = key.substr(5);
    if (numberText.empty())
        return fallback;
    try
    {
        const int value = std::stoi(numberText);
        if (value >= 1 && value <= 255)
            return static_cast<char>(static_cast<unsigned char>(value));
    }
    catch (...) {}
    return fallback;
}

char sectorSymbolFromJson(const json& source,
                          const char* stringName,
                          const char* codeName,
                          char fallback = 'A')
{
    if (source.contains(codeName) && source[codeName].is_number_integer())
    {
        const int code = source[codeName].get<int>();
        if (code >= 1 && code <= 255)
            return static_cast<char>(static_cast<unsigned char>(code));
    }
    if (source.contains(stringName) && source[stringName].is_string())
        return sectorSymbolFromKey(source[stringName].get<std::string>(), fallback);
    return fallback;
}

std::string sectorDisplayLabel(char symbol)
{
    const int code = sectorCode(symbol);
    std::string label;
    if (code >= 33 && code <= 126)
    {
        label.push_back(symbol);
        label += " (#" + std::to_string(code) + ")";
    }
    else
    {
        label = "#" + std::to_string(code);
    }
    return label;
}

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::uint32_t stableSeed(const std::string& value)
{
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : value)
    {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash == 0u ? 1u : hash;
}

std::uint32_t nextRandom(std::uint32_t& state)
{
    if (state == 0u) state = 0x6d2b79f5u;
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

double randomUnit(std::uint32_t& state)
{
    return static_cast<double>(nextRandom(state) & 0x00ffffffu) / 16777215.0;
}
}

BuildInteriorEngine::~BuildInteriorEngine()
{
    shutdown();
}

bool BuildInteriorEngine::init(SDL_Window* window, SDL_Renderer* renderer)
{
    m_window = window;
    m_renderer = renderer;
    m_lastError.clear();
    m_status.clear();

    if (!m_window || !m_renderer)
    {
        m_lastError = "BuildInteriorEngine init failed: missing SDL window or renderer.";
        return false;
    }

    m_textureSpriteEditor.init(m_renderer, pathutils::ProjectRoot());

    // Stage 1 starts directly on the data-driven Houska exterior. The old
    // test chamber remains a safe fallback while the castle maps are built in
    // independent stages.
    bool loaded = loadInterior("castle:houska_1400/houska_exterior");
    if (!loaded)
    {
        SDL_Log("Houska exterior load failed, falling back to test_chamber: %s",
                m_lastError.c_str());
        loaded = loadInterior("test_chamber");
    }

    if (loaded)
    {
        m_sceneDirty = true;
        setMouseLookEnabled(!m_editorMode);
    }
    return loaded;
}

void BuildInteriorEngine::setEditorMode(bool enabled)
{
    if (m_editorMode == enabled)
        return;

    m_editorMode = enabled;
    setMouseLookEnabled(!enabled);
    if (enabled)
        m_editorRefreshMapListRequested = true;
    m_sceneDirty = true;
}

void BuildInteriorEngine::setRuntimeOverlayVisible(bool visible)
{
    m_runtimeOverlayVisible = visible;
}

void BuildInteriorEngine::setPlayerStats(const PlayerStats* stats)
{
    m_playerStats = stats;
}

std::string BuildInteriorEngine::currentInteriorLocationId() const
{
    if (!m_loadedCastleId.empty() && !m_loadedCastleMapId.empty())
        return "castle:" + m_loadedCastleId + "/" + m_loadedCastleMapId;

    if (!m_loadedPath.empty())
        return m_loadedPath;

    return m_currentInteriorId;
}

void BuildInteriorEngine::setMouseLookEnabled(bool enabled)
{
    if (m_editorMode || m_mouseLookSuppressed)
        enabled = false;

    if (!enabled)
    {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        m_mouseLook = false;
        return;
    }

    m_mouseLook = SDL_SetRelativeMouseMode(SDL_TRUE) == 0;
}

void BuildInteriorEngine::setMouseLookSuppressed(bool suppressed)
{
    if (m_mouseLookSuppressed == suppressed)
        return;

    m_mouseLookSuppressed = suppressed;
    setMouseLookEnabled(!m_editorMode && !m_mouseLookSuppressed);
}

void BuildInteriorEngine::getPlayerPose(double& outX, double& outY, double& outAngle, double& outPitch) const
{
    outX = m_posX;
    outY = m_posY;
    outAngle = m_angle;
    outPitch = m_pitch;
}

void BuildInteriorEngine::setPlayerPose(double x, double y, double angle, double pitch)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(angle) || !std::isfinite(pitch))
        return;

    m_posX = x;
    m_posY = y;
    m_angle = wrapAngle(angle);
    m_pitch = std::clamp(pitch, -0.52, 0.52);
    m_lastSafeX = x;
    m_lastSafeY = y;
    m_moveBlend = 0.0;
    m_verticalVelocity = 0.0;
    m_grounded = true;
    m_playerZInitialized = false;
    m_cameraFloorInitialized = false;
    m_sceneDirty = true;
}

void BuildInteriorEngine::shutdown()
{
    setMouseLookEnabled(false);
    m_textureSpriteEditor.shutdown();

    if (m_sceneTexture)
    {
        SDL_DestroyTexture(m_sceneTexture);
        m_sceneTexture = nullptr;
    }
    m_sceneW = 0;
    m_sceneH = 0;
    m_framebuffer.clear();
    m_zBuffer.clear();
    m_dynamicDepthBuffer.clear();

    clearTextures();
    m_grid.clear();
    m_sectorGrid.clear();
    m_defaultSolidTexture = kNoTexture;
    m_sectors.clear();
    m_sectorLookup.fill(nullptr);
    m_doors.clear();
    m_wallSegments.clear();
    m_polygonBoundaryWalls.clear();
    m_polygonSectors.clear();
    m_polygonPortals.clear();
    m_traversalRamps.clear();
    m_vectorSectorLookup.clear();
    m_vectorSectorLookupW = 0;
    m_vectorSectorLookupH = 0;
    m_vectorLookupOriginX = 0.0;
    m_vectorLookupOriginY = 0.0;
    m_sprites.clear();
    m_window = nullptr;
    m_renderer = nullptr;
}

void BuildInteriorEngine::handleEvent(const SDL_Event& e)
{
    m_textureSpriteEditor.handleEvent(e);
    if (m_textureSpriteEditor.isOpen())
        return;
    if (m_editorMode)
        return;
    if (m_mouseLookSuppressed)
    {
        setMouseLookEnabled(false);
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const bool mouseEvent =
        e.type == SDL_MOUSEBUTTONDOWN ||
        e.type == SDL_MOUSEBUTTONUP ||
        e.type == SDL_MOUSEMOTION ||
        e.type == SDL_MOUSEWHEEL;
    if (mouseEvent && io.WantCaptureMouse)
        return;

    if (e.type == SDL_WINDOWEVENT)
    {
        if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            setMouseLookEnabled(false);
        else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
            setMouseLookEnabled(true);
        return;
    }

    // Standard FPS behaviour: the mouse is captured continuously while the
    // game view is active. Escape releases it; clicking the view captures it
    // again if the surrounding game state did not already change screens.
    if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
    {
        setMouseLookEnabled(false);
        return;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && !m_mouseLook)
    {
        setMouseLookEnabled(true);
        return;
    }
    if (e.type == SDL_MOUSEMOTION && m_mouseLook)
    {
        m_angle = wrapAngle(m_angle + static_cast<double>(e.motion.xrel) * 0.0036);
        m_pitch = std::clamp(m_pitch - static_cast<double>(e.motion.yrel) * 0.0028, -0.52, 0.52);
        m_sceneDirty = true;
    }
}

void BuildInteriorEngine::update(float dt)
{
    if (dt > 0.00001f && dt < 0.50f)
    {
        const double instantFps = 1.0 / static_cast<double>(dt);
        m_smoothedFps = m_smoothedFps <= 0.0
            ? instantFps
            : m_smoothedFps + (instantFps - m_smoothedFps) * 0.08;
    }

    // A debugger pause or a dragged window must not teleport the player or
    // advance door animation by a huge step.
    dt = std::clamp(dt, 0.0f, 0.05f);
    m_playerMoving = false;
    m_playerRunning = false;

    if (m_mouseLookSuppressed)
    {
        m_wasUseKeyDown = false;
        m_wasRecoverKeyDown = false;
        m_wasJumpKeyDown = false;
        return;
    }

    const double oldPosX = m_posX;
    const double oldPosY = m_posY;
    const double oldAngle = m_angle;
    const double oldPitch = m_pitch;
    const double oldPlayerZ = m_playerZ;
    const double oldCameraFloorZ = m_cameraFloorZ;
    const double oldBobPhase = m_bobPhase;
    const double oldMoveBlend = m_moveBlend;

    updateDoors(dt);
    updateAnimatedEffects(dt);

    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    // WantCaptureKeyboard can remain true merely because the editor window has
    // focus. Suppress movement only while the user is typing into a text field.
    const bool keyboardCaptured = m_editorMode && ImGui::GetIO().WantTextInput;

    if (!m_editorMode)
    {
        const bool useDown = keys[SDL_SCANCODE_E] != 0 || keys[SDL_SCANCODE_RETURN] != 0;
        if (useDown && !m_wasUseKeyDown)
            useNearestInteraction();
        m_wasUseKeyDown = useDown;
    }

    const bool recoverDown = keys[SDL_SCANCODE_HOME] != 0;
    if (recoverDown && !m_wasRecoverKeyDown)
        recoverToLastSafePosition();
    m_wasRecoverKeyDown = recoverDown;

    if (keyboardCaptured)
        return;

    if (!m_playerZInitialized)
    {
        m_playerZ = floorHeightAtWorld(m_posX, m_posY);
        m_playerZInitialized = true;
        m_grounded = true;
    }

    const bool jumpDown = keys[SDL_SCANCODE_SPACE] != 0;
    if (jumpDown && !m_wasJumpKeyDown && m_grounded)
    {
        m_verticalVelocity = kJumpVelocity;
        m_grounded = false;
        m_status = U8("Skok.");
    }
    m_wasJumpKeyDown = jumpDown;

    const double forwardX = std::cos(m_angle);
    const double forwardY = std::sin(m_angle);
    const double rightX = -std::sin(m_angle);
    const double rightY = std::cos(m_angle);

    const bool running = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    double moveSpeed = m_editorMode ? 3.4 : 2.55;
    if (!m_editorMode && m_playerStats)
        moveSpeed = static_cast<double>(m_playerStats->getLimitedMoveSpeed(running)) * kInteriorUnitsPerCampaignPixel;
    else if (running)
        moveSpeed *= 1.55;
    const double step = moveSpeed * static_cast<double>(dt);
    const double turnSpeed = 2.30 * static_cast<double>(dt);

    double moveX = 0.0;
    double moveY = 0.0;
    if (keys[SDL_SCANCODE_W]) { moveX += forwardX; moveY += forwardY; }
    if (keys[SDL_SCANCODE_S]) { moveX -= forwardX; moveY -= forwardY; }
    if (keys[SDL_SCANCODE_A]) { moveX -= rightX;   moveY -= rightY; }
    if (keys[SDL_SCANCODE_D]) { moveX += rightX;   moveY += rightY; }

    const double moveLength = std::hypot(moveX, moveY);
    const bool requested = moveLength > 0.0001;
    m_playerMoving = !m_editorMode && requested;
    m_playerRunning = m_playerMoving && running;
    if (requested)
    {
        // Normalize diagonal movement and do only one collision/sliding pass.
        // This is both more FPS-like and cheaper than four independent moves.
        tryMove(moveX / moveLength * step, moveY / moveLength * step);
    }

    // Keyboard looking remains as an accessibility/debug fallback. Normal play
    // uses relative mouse motion captured by handleEvent().
    if (keys[SDL_SCANCODE_LEFT])  m_angle = wrapAngle(m_angle - turnSpeed);
    if (keys[SDL_SCANCODE_RIGHT]) m_angle = wrapAngle(m_angle + turnSpeed);
    if (keys[SDL_SCANCODE_UP])    m_pitch = std::clamp(m_pitch + 0.9 * dt, -0.52, 0.52);
    if (keys[SDL_SCANCODE_DOWN])  m_pitch = std::clamp(m_pitch - 0.9 * dt, -0.52, 0.52);

    // Vertical movement is still constrained to a single floor/ceiling pair.
    // Space adds a vertical impulse; gravity then returns the player to the
    // sloped or flat floor below him.
    const double groundZ = floorHeightAtWorld(m_posX, m_posY);
    const double ceilingZ = ceilingHeightAtWorld(m_posX, m_posY);
    if (!m_grounded)
    {
        m_verticalVelocity -= kGravity * static_cast<double>(dt);
        m_playerZ += m_verticalVelocity * static_cast<double>(dt);

        const double maxFeetZ = ceilingZ - kPlayerBodyHeight - kMinimumHeadroom;
        if (m_playerZ > maxFeetZ)
        {
            m_playerZ = maxFeetZ;
            if (m_verticalVelocity > 0.0)
                m_verticalVelocity = 0.0;
        }

        if (m_playerZ <= groundZ)
        {
            m_playerZ = groundZ;
            m_verticalVelocity = 0.0;
            m_grounded = true;
        }
    }
    else
    {
        // Follow ramps and stairs exactly while grounded.
        m_playerZ = groundZ;
    }

    const double followSpeed = m_grounded ? 13.0 : 22.0;
    const double follow = 1.0 - std::exp(-followSpeed * std::max(0.0f, dt));
    if (m_grounded && m_playerZ > m_cameraFloorZ)
    {
        // Never let the eye lag below a rising floor. The old cosmetic camera
        // smoothing put the view inside an otherwise correct ramp while
        // walking uphill, which looked as if the player passed through raised
        // terrain. Smooth traversal heights already keep the motion itself
        // continuous; only downward settling needs interpolation.
        m_cameraFloorZ = m_playerZ;
    }
    else
    {
        m_cameraFloorZ += (m_playerZ - m_cameraFloorZ) * follow;
    }
    if (std::abs(m_playerZ - m_cameraFloorZ) < 0.0001)
        m_cameraFloorZ = m_playerZ;

    const double targetBlend = requested ? 1.0 : 0.0;
    m_moveBlend += (targetBlend - m_moveBlend) * std::min(1.0, static_cast<double>(dt) * 8.0);
    if (std::abs(targetBlend - m_moveBlend) < 0.001)
        m_moveBlend = targetBlend;
    if (requested && m_grounded)
        m_bobPhase += static_cast<double>(dt) * (running ? 11.0 : 8.0);

    if (std::abs(m_posX - oldPosX) > 1.0e-8 ||
        std::abs(m_posY - oldPosY) > 1.0e-8 ||
        std::abs(m_angle - oldAngle) > 1.0e-8 ||
        std::abs(m_pitch - oldPitch) > 1.0e-8 ||
        std::abs(m_playerZ - oldPlayerZ) > 1.0e-8 ||
        std::abs(m_cameraFloorZ - oldCameraFloorZ) > 1.0e-8 ||
        std::abs(m_bobPhase - oldBobPhase) > 1.0e-8 ||
        std::abs(m_moveBlend - oldMoveBlend) > 1.0e-8)
    {
        m_sceneDirty = true;
    }
}

void BuildInteriorEngine::render()
{
    if (!m_renderer)
        return;

    int outputW = 0;
    int outputH = 0;
    SDL_GetRendererOutputSize(m_renderer, &outputW, &outputH);
    if (outputW <= 0 || outputH <= 0)
        return;

    // The first version rendered the software framebuffer at the full window
    // resolution. At 1920x1080 that means more than two million floor/ceiling
    // samples every frame, which is brutal in a Debug build. Use an internal
    // resolution and let SDL upscale the finished scene.
    const bool draw3D = !m_editorMode || m_editorPreview3D;
    if (draw3D)
    {
        const float scale = std::clamp(m_editorMode ? m_editorRenderScale
                                                   : m_gameRenderScale,
                                       0.15f, 1.00f);
        int renderW = std::max(320, static_cast<int>(std::lround(outputW * scale)));
        int renderH = std::max(180, static_cast<int>(std::lround(outputH * scale)));

        // Keep the internal buffer reasonably bounded on high-DPI/4K displays.
        // 960 software-rendered columns are already plenty after SDL upscaling.
        const int kMaxInternalWidth = m_editorMode ? 800 : 800;
        if (renderW > kMaxInternalWidth)
        {
            const double ratio = static_cast<double>(kMaxInternalWidth) / renderW;
            renderW = kMaxInternalWidth;
            renderH = std::max(180, static_cast<int>(std::lround(renderH * ratio)));
        }

        // A static software-rendered scene is reusable. This makes standing
        // still essentially free while ImGui and the rest of the game continue
        // to render at the normal application frame rate.
        // In the editor we no longer re-rasterize every frame while the
        // camera is standing still. The preview is cached exactly like the
        // in-game scene and only rebuilt after an actual edit / movement /
        // resolution change. This keeps the editor responsive even on large
        // maps and avoids the unnecessary FPS collapse that happened before.
        const bool mustRasterize = m_sceneDirty || !m_sceneTexture ||
                                   renderW != m_sceneW || renderH != m_sceneH;
        if (mustRasterize)
        {
            renderScene(renderW, renderH);
            m_sceneDirty = false;
        }
        else
        {
            SDL_RenderCopy(m_renderer, m_sceneTexture, nullptr, nullptr);
        }
    }
    else
    {
        SDL_SetRenderDrawColor(m_renderer, 14, 13, 12, 255);
        SDL_RenderClear(m_renderer);
    }

    if (m_editorMode)
        renderEditorOverlay();
    else
        renderOverlay();
}

bool BuildInteriorEngine::loadInterior(const std::string& interiorIdOrPath)
{
    return loadInteriorAtSpawn(interiorIdOrPath, std::string());

    m_textureSpriteEditor.render();
    if (m_textureSpriteEditor.consumeReloadRequested() && !m_currentInteriorId.empty())
        loadInterior(m_currentInteriorId);
}

bool BuildInteriorEngine::consumePendingCampaignTransition(std::string& outMap, std::string& outSpawnId)
{
    if (m_pendingCampaignTransitionMap.empty())
        return false;

    outMap = m_pendingCampaignTransitionMap;
    outSpawnId = m_pendingCampaignTransitionSpawnId;
    m_pendingCampaignTransitionMap.clear();
    m_pendingCampaignTransitionSpawnId.clear();
    return true;
}

void BuildInteriorEngine::swapLoadedMapState(BuildInteriorEngine& other)
{
    using std::swap;

    swap(m_currentInteriorId, other.m_currentInteriorId);
    swap(m_displayName, other.m_displayName);
    swap(m_loadedPath, other.m_loadedPath);
    swap(m_loadedCastleId, other.m_loadedCastleId);
    swap(m_loadedCastleMapId, other.m_loadedCastleMapId);
    swap(m_lastError, other.m_lastError);
    swap(m_status, other.m_status);

    swap(m_grid, other.m_grid);
    swap(m_sectorGrid, other.m_sectorGrid);
    swap(m_defaultSolidTexture, other.m_defaultSolidTexture);
    swap(m_textures, other.m_textures);
    swap(m_texturePaths, other.m_texturePaths);
    swap(m_sectors, other.m_sectors);
    swap(m_doors, other.m_doors);
    swap(m_wallSegments, other.m_wallSegments);
    swap(m_polygonBoundaryWalls, other.m_polygonBoundaryWalls);
    swap(m_polygonSectors, other.m_polygonSectors);
    swap(m_polygonPortals, other.m_polygonPortals);
    swap(m_traversalRamps, other.m_traversalRamps);
    swap(m_floorCutoutSectors, other.m_floorCutoutSectors);
    swap(m_floorCutoutPolygonIndices, other.m_floorCutoutPolygonIndices);
    swap(m_floorCutoutMinX, other.m_floorCutoutMinX);
    swap(m_floorCutoutMinY, other.m_floorCutoutMinY);
    swap(m_floorCutoutMaxX, other.m_floorCutoutMaxX);
    swap(m_floorCutoutMaxY, other.m_floorCutoutMaxY);
    swap(m_sprites, other.m_sprites);
    swap(m_namedSpawns, other.m_namedSpawns);

    swap(m_vectorSectorLookupW, other.m_vectorSectorLookupW);
    swap(m_vectorSectorLookupH, other.m_vectorSectorLookupH);
    swap(m_vectorLookupOriginX, other.m_vectorLookupOriginX);
    swap(m_vectorLookupOriginY, other.m_vectorLookupOriginY);
    swap(m_vectorSectorLookup, other.m_vectorSectorLookup);

    swap(m_posX, other.m_posX);
    swap(m_posY, other.m_posY);
    swap(m_angle, other.m_angle);
    swap(m_fov, other.m_fov);
    swap(m_eyeHeight, other.m_eyeHeight);
    swap(m_pitch, other.m_pitch);
    swap(m_bobPhase, other.m_bobPhase);
    swap(m_moveBlend, other.m_moveBlend);
    swap(m_cameraFloorZ, other.m_cameraFloorZ);
    swap(m_cameraFloorInitialized, other.m_cameraFloorInitialized);
    swap(m_lastSafeX, other.m_lastSafeX);
    swap(m_lastSafeY, other.m_lastSafeY);
    swap(m_playerZ, other.m_playerZ);
    swap(m_verticalVelocity, other.m_verticalVelocity);
    swap(m_grounded, other.m_grounded);
    swap(m_playerZInitialized, other.m_playerZInitialized);

    // Lookup tables store pointers into the unordered maps. Rebuild both sides
    // after the map containers were swapped so the temporary engine can safely
    // destroy the old map while this instance keeps the new one.
    rebuildTextureLookup();
    rebuildSectorLookup();
    other.rebuildTextureLookup();
    other.rebuildSectorLookup();
    m_renderLights.clear();
}

bool BuildInteriorEngine::loadInteriorAtSpawn(const std::string& interiorIdOrPath, const std::string& spawnId)
{
    bool loaded = false;
    std::string failure;

    {
        BuildInteriorEngine candidate;
        candidate.m_window = m_window;
        candidate.m_renderer = m_renderer;
        candidate.m_editorMode = m_editorMode;
        candidate.m_gameRenderScale = m_gameRenderScale;
        candidate.m_editorRenderScale = m_editorRenderScale;
        candidate.m_fastFloorCasting = m_fastFloorCasting;

        try
        {
            loaded = candidate.loadInteriorAtSpawnUnsafe(interiorIdOrPath, spawnId);
            if (!loaded)
                failure = candidate.m_lastError.empty()
                    ? std::string("Map load failed without a detailed error.")
                    : candidate.m_lastError;
        }
        catch (const std::exception& ex)
        {
            failure = std::string("Map load exception: ") + ex.what();
            loaded = false;
        }
        catch (...)
        {
            failure = "Map load failed with an unknown exception.";
            loaded = false;
        }

        if (loaded)
            swapLoadedMapState(candidate);
    }

    // The temporary loader releases relative mouse mode in its destructor.
    // Restore the expected state only after it is gone.
    setMouseLookEnabled(!m_editorMode);

    if (!loaded)
    {
        m_lastError = failure;
        m_status = failure;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "2.5D map load failed for %s: %s",
                     interiorIdOrPath.c_str(), failure.c_str());
        return false;
    }

    sanitizeEditorState();
    rebuildRenderLights();
    m_sceneDirty = true;
    return true;
}

bool BuildInteriorEngine::requestCampaignTransitionIfNeeded(const std::string& targetLocation, const std::string& spawnId)
{
    std::string targetMap;
    if (!parseCampaignLocation(targetLocation, targetMap))
        return false;

    m_pendingCampaignTransitionMap = targetMap;
    m_pendingCampaignTransitionSpawnId = spawnId;
    m_status = U8("Návrat do 2D mapy: ") + targetMap;
    return true;
}

bool BuildInteriorEngine::loadInteriorAtSpawnUnsafe(const std::string& interiorIdOrPath, const std::string& spawnId)
{
    // HOUSKA_V3_CASTLE_SOURCE_REDIRECT
    // The editor can return a physical source path such as
    // data/castles/houska_1400/maps/houska_courtyard_ground.map.json.
    // Route it through the castle pipeline instead of treating schema v2 as a legacy grid map.
    {
        std::string normalizedSource = interiorIdOrPath;
        std::replace(normalizedSource.begin(), normalizedSource.end(), '\\', '/');
        const std::string castleMarker = "data/castles/";
        const std::string mapSuffix = ".map.json";
        const std::size_t castlePos = normalizedSource.find(castleMarker);
        const bool isCastleSource =
            castlePos != std::string::npos &&
            normalizedSource.size() >= mapSuffix.size() &&
            normalizedSource.compare(normalizedSource.size() - mapSuffix.size(), mapSuffix.size(), mapSuffix) == 0;

        if (isCastleSource)
        {
            const std::size_t castleIdBegin = castlePos + castleMarker.size();
            const std::size_t mapsMarker = normalizedSource.find("/maps/", castleIdBegin);
            if (mapsMarker != std::string::npos && mapsMarker > castleIdBegin)
            {
                const std::string castleId = normalizedSource.substr(castleIdBegin, mapsMarker - castleIdBegin);
                const std::size_t mapIdBegin = mapsMarker + 6;
                std::string mapId = normalizedSource.substr(mapIdBegin);
                mapId.erase(mapId.size() - mapSuffix.size());
                if (!castleId.empty() && !mapId.empty())
                    return loadInteriorAtSpawnUnsafe("castle:" + castleId + "/" + mapId, spawnId);
            }
        }
    }

    interior::CastleLocationRef castleLocation;
    if (interior::ParseCastleLocation(interiorIdOrPath, castleLocation))
    {
        interior::InteriorMapDef sourceMap;
        interior::CastleProjectDef castleProject;
        std::string pipelineError;

        if (!interior::LoadCastleMap(
                ProjectRootPath(),
                castleLocation.castleId,
                castleLocation.mapId,
                sourceMap,
                castleProject,
                pipelineError))
        {
            m_lastError = "Castle map load failed: " + pipelineError;
            return false;
        }

        auto loadLooseCastleTextures = [&]()
        {
            if (!m_renderer)
                return;

            std::vector<fs::path> assetRoots;
            std::unordered_set<std::string> seenRoots;
            for (const auto& entry : castleProject.materials)
            {
                if (entry.second.texturePath.empty())
                    continue;

                const fs::path texturePath = fs::path(entry.second.texturePath).lexically_normal();
                std::vector<fs::path> parts;
                for (const auto& part : texturePath)
                    parts.push_back(part);

                fs::path root;
                if (parts.size() >= 3 &&
                    lowerAscii(parts[0].generic_string()) == "assets" &&
                    lowerAscii(parts[1].generic_string()) == "castles")
                {
                    root = parts[0] / parts[1] / parts[2];
                }
                else
                {
                    root = texturePath.parent_path();
                }

                const std::string normalizedRoot = lowerAscii(root.lexically_normal().generic_string());
                if (!root.empty() && seenRoots.insert(normalizedRoot).second)
                    assetRoots.push_back(root);
            }

            std::unordered_set<std::string> loadedPaths;
            for (const auto& entry : m_texturePaths)
                loadedPaths.insert(lowerAscii(fs::path(entry.second).lexically_normal().generic_string()));

            TextureKey nextTextureKey = 512;
            for (const fs::path& assetRoot : assetRoots)
            {
                const fs::path absoluteRoot = ProjectRootPath() / assetRoot;
                std::error_code ec;
                if (!fs::exists(absoluteRoot, ec) || ec)
                    continue;

                for (fs::recursive_directory_iterator it(absoluteRoot, ec), end; !ec && it != end; it.increment(ec))
                {
                    if (it->is_directory(ec) || ec)
                        continue;
                    if (lowerAscii(it->path().extension().generic_string()) != ".png")
                        continue;

                    std::error_code relativeError;
                    fs::path relativePath = fs::relative(it->path(), ProjectRootPath(), relativeError);
                    if (relativeError)
                        relativePath = it->path();

                    const std::string texturePath = relativePath.lexically_normal().generic_string();
                    const std::string normalizedTexture = lowerAscii(fs::path(texturePath).lexically_normal().generic_string());
                    if (!loadedPaths.insert(normalizedTexture).second)
                        continue;

                    while (m_textures.find(nextTextureKey) != m_textures.end() && nextTextureKey < 65535)
                        ++nextTextureKey;
                    if (nextTextureKey >= 65535)
                        return;

                    loadTextureForCell(nextTextureKey, texturePath);
                    ++nextTextureKey;
                }
            }
        };

        interior::CompileResult compiled = interior::CompileCastleMapToLegacy(
            ProjectRootPath(), castleProject, sourceMap);
        if (!compiled.success)
        {
            // Compatibility bridge for newly introduced runtime features such
            // as hinged doors. Older castle-pipeline validators may reject a
            // source value before the legacy adapter gets a chance to compile
            // it. Prefer the last known-good compiled preview when available,
            // so gameplay remains usable instead of silently keeping the
            // previously loaded map.
            const fs::path fallbackPath = ProjectRootPath() /
                "data" / "castles" / castleLocation.castleId /
                ".compiled" / (castleLocation.mapId + ".json");

            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Castle source compile failed for %s/%s: %s",
                        castleLocation.castleId.c_str(),
                        castleLocation.mapId.c_str(),
                        compiled.error.c_str());

            std::error_code existsError;
            if (fs::exists(fallbackPath, existsError) && !existsError)
            {
                const bool loadedFallback = loadInteriorAtSpawnUnsafe(fallbackPath.string(), spawnId);
                if (loadedFallback)
                {
                    m_loadedCastleId = castleLocation.castleId;
                    m_loadedCastleMapId = castleLocation.mapId;
                    loadCastleEntityOverlay(castleLocation.castleId, castleLocation.mapId);
                    loadCastleGeometryOverlay(castleLocation.castleId, castleLocation.mapId);
                    loadLooseCastleTextures();
                    m_editorDefaultSpawnDirty = false;
                    m_currentInteriorId = sourceMap.id;
                    m_displayName = sourceMap.displayName;
                    m_lastError.clear();
                    m_status = U8("Načtena poslední přeložená verze mapy. "
                                  "Zdrojový validátor zatím nezná otočné dveře.");
                }
                return loadedFallback;
            }

            m_lastError = "Castle map compile failed: " + compiled.error;
            return false;
        }

        SDL_Log("Interior castle pipeline: %s", compiled.validation.summary().c_str());
        const bool loaded = loadInteriorAtSpawnUnsafe(compiled.outputPath.string(), spawnId);
        if (loaded)
        {
            m_loadedCastleId = castleLocation.castleId;
            m_loadedCastleMapId = castleLocation.mapId;
            loadCastleEntityOverlay(castleLocation.castleId, castleLocation.mapId);
            loadCastleGeometryOverlay(castleLocation.castleId, castleLocation.mapId);
            loadLooseCastleTextures();
            m_editorDefaultSpawnDirty = false;
            m_currentInteriorId = sourceMap.id;
            m_displayName = sourceMap.displayName;
            m_status = "Loaded castle map: " + sourceMap.displayName;
        }
        return loaded;
    }

    const std::string path = resolveInteriorPath(interiorIdOrPath);
    std::ifstream in(path);
    if (!in)
    {
        m_lastError = "Cannot open interior map: " + path;
        return false;
    }

    json root;
    try
    {
        in >> root;
    }
    catch (const std::exception& ex)
    {
        m_lastError = std::string("Interior JSON parse failed: ") + ex.what();
        return false;
    }

    // Validate both the modern uint16 grid_codes format and the old one-character grid.
    // Do not clear the currently displayed map until the candidate has passed validation.
    const bool hasNumericGrid = root.contains("grid_codes") &&
        root["grid_codes"].is_array() && !root["grid_codes"].empty();
    const bool hasLegacyGrid = root.contains("grid") &&
        root["grid"].is_array() && !root["grid"].empty();
    const bool hasPolygonGeometry = root.contains("polygon_sectors") &&
        root["polygon_sectors"].is_array() && !root["polygon_sectors"].empty();

    if (!hasNumericGrid && !hasLegacyGrid && !hasPolygonGeometry)
    {
        m_lastError = "Selected JSON is neither a compiled interior nor a polygon interior. Open castle source maps through castle:<castle>/<map>.";
        m_status = m_lastError;
        return false;
    }

    std::size_t expectedWidth = 0;
    std::size_t expectedHeight = 0;

    if (hasNumericGrid)
    {
        expectedHeight = root["grid_codes"].size();
        for (const json& row : root["grid_codes"])
        {
            if (!row.is_array())
            {
                m_lastError = "Interior grid_codes contains a non-array row.";
                m_status = m_lastError;
                return false;
            }

            const std::size_t width = row.size();
            if (expectedWidth == 0) expectedWidth = width;
            if (width == 0 || width != expectedWidth)
            {
                m_lastError = "Interior grid_codes is empty or has inconsistent row widths.";
                m_status = m_lastError;
                return false;
            }

            for (const json& value : row)
            {
                if (!value.is_number_integer())
                {
                    m_lastError = "Interior grid_codes contains a non-integer texture key.";
                    m_status = m_lastError;
                    return false;
                }
                const int key = value.get<int>();
                if (key < 0 || key > 65535)
                {
                    m_lastError = "Interior grid_codes contains a texture key outside 0..65535.";
                    m_status = m_lastError;
                    return false;
                }
            }
        }
    }
    else if (hasLegacyGrid)
    {
        expectedHeight = root["grid"].size();
        for (const json& row : root["grid"])
        {
            if (!row.is_string())
            {
                m_lastError = "Interior legacy grid contains a non-string row.";
                m_status = m_lastError;
                return false;
            }
            const std::size_t width = row.get_ref<const std::string&>().size();
            if (expectedWidth == 0) expectedWidth = width;
            if (width == 0 || width != expectedWidth)
            {
                m_lastError = "Interior legacy grid is empty or has inconsistent row widths.";
                m_status = m_lastError;
                return false;
            }
        }
    }

    if (root.contains("sector_grid") && expectedHeight > 0)
    {
        if (!root["sector_grid"].is_array() ||
            root["sector_grid"].size() != expectedHeight)
        {
            m_lastError = "Interior sector_grid has an invalid row count.";
            m_status = m_lastError;
            return false;
        }
        for (const json& row : root["sector_grid"])
        {
            if (!row.is_string() || row.get_ref<const std::string&>().size() != expectedWidth)
            {
                m_lastError = "Interior sector_grid has inconsistent row widths.";
                m_status = m_lastError;
                return false;
            }
        }
    }
    if (root.contains("sector_codes") && expectedHeight > 0)
    {
        if (!root["sector_codes"].is_array() ||
            root["sector_codes"].size() != expectedHeight)
        {
            m_lastError = "Interior sector_codes has an invalid row count.";
            m_status = m_lastError;
            return false;
        }
        for (const json& row : root["sector_codes"])
        {
            if (!row.is_array() || row.size() != expectedWidth)
            {
                m_lastError = "Interior sector_codes has inconsistent row widths.";
                m_status = m_lastError;
                return false;
            }
            for (const json& value : row)
            {
                if (!value.is_number_integer())
                {
                    m_lastError = "Interior sector_codes contains a non-integer sector code.";
                    m_status = m_lastError;
                    return false;
                }
                const int code = value.get<int>();
                if (code < 1 || code > 255)
                {
                    m_lastError = "Interior sector_codes contains a sector code outside 1..255.";
                    m_status = m_lastError;
                    return false;
                }
            }
        }
    }

    if (root.contains("textures") &&
        !root["textures"].is_object() && !root["textures"].is_array())
    {
        m_lastError = "Interior textures must be a JSON object or array.";
        m_status = m_lastError;
        return false;
    }
    if (root.contains("sectors") && !root["sectors"].is_object())
    {
        m_lastError = "Interior sectors must be a JSON object.";
        m_status = m_lastError;
        return false;
    }

    clearTextures();
    m_grid.clear();
    m_sectorGrid.clear();
    m_sectors.clear();
    m_sectorLookup.fill(nullptr);
    m_doors.clear();
    m_wallSegments.clear();
    m_polygonBoundaryWalls.clear();
    m_polygonSectors.clear();
    m_polygonPortals.clear();
    m_traversalRamps.clear();
    m_floorCutoutSectors.fill(false);
    m_floorCutoutPolygonIndices.clear();
    m_floorCutoutMinX = 1.0e30;
    m_floorCutoutMinY = 1.0e30;
    m_floorCutoutMaxX = -1.0e30;
    m_floorCutoutMaxY = -1.0e30;
    m_vectorSectorLookup.clear();
    m_vectorSectorLookupW = 0;
    m_vectorSectorLookupH = 0;
    m_vectorLookupOriginX = 0.0;
    m_vectorLookupOriginY = 0.0;
    m_sprites.clear();
    m_namedSpawns.clear();
    m_texturePaths.clear();
    m_lastError.clear();
    m_status.clear();

    m_loadedPath = path;
    // For direct/legacy JSON maps there is no editable castle geometry overlay.
    // Castle callers restore these identifiers after the compiled preview loads.
    m_loadedCastleId.clear();
    m_loadedCastleMapId.clear();
    m_currentInteriorId = root.value("id", interiorIdOrPath);
    m_displayName = root.value("name", m_currentInteriorId);
    m_eyeHeight = std::clamp(root.value("eye_height", 0.56), 0.25, 2.20);
    m_defaultSolidTexture = static_cast<TextureKey>(
        std::clamp(root.value("default_solid_texture_key", 0), 0, 65535));

    if (root.contains("spawn") && root["spawn"].is_object())
    {
        const json& spawn = root["spawn"];
        m_posX = spawn.value("x", 2.5);
        m_posY = spawn.value("y", 2.5);
        m_angle = spawn.value("angle_degrees", 0.0) * kPi / 180.0;
        m_pitch = std::clamp(spawn.value("pitch", 0.0), -0.52, 0.52);
    }

    if (root.contains("spawns") && root["spawns"].is_object())
    {
        for (auto it = root["spawns"].begin(); it != root["spawns"].end(); ++it)
        {
            if (!it.value().is_object()) continue;
            SpawnDef spawn;
            spawn.x = it.value().value("x", 2.5);
            spawn.y = it.value().value("y", 2.5);
            spawn.angle = it.value().value("angle_degrees", 0.0) * kPi / 180.0;
            spawn.pitch = std::clamp(it.value().value("pitch", 0.0), -0.52, 0.52);
            m_namedSpawns[it.key()] = spawn;
        }
    }
    if (!spawnId.empty())
    {
        const auto spawnIt = m_namedSpawns.find(spawnId);
        if (spawnIt != m_namedSpawns.end())
        {
            m_posX = spawnIt->second.x;
            m_posY = spawnIt->second.y;
            m_angle = spawnIt->second.angle;
            m_pitch = spawnIt->second.pitch;
        }
    }

    m_fov = std::clamp(root.value("fov_degrees", 72.0), 45.0, 95.0) * kPi / 180.0;

    const bool rootHasPolygonGeometry = root.contains("polygon_sectors") &&
        root["polygon_sectors"].is_array() && !root["polygon_sectors"].empty();
    if (root.contains("grid_codes") && root["grid_codes"].is_array())
    {
        for (const auto& row : root["grid_codes"])
        {
            if (!row.is_array()) continue;
            std::vector<TextureKey> decoded;
            decoded.reserve(row.size());
            for (const auto& value : row)
                decoded.push_back(static_cast<TextureKey>(std::clamp(value.get<int>(), 0, 65535)));
            m_grid.push_back(std::move(decoded));
        }
    }
    else if (root.contains("grid") && root["grid"].is_array())
    {
        for (const auto& row : root["grid"])
        {
            const std::string legacy = row.get<std::string>();
            std::vector<TextureKey> decoded;
            decoded.reserve(legacy.size());
            for (unsigned char c : legacy)
                decoded.push_back(isEmptyCell(c) ? kNoTexture : static_cast<TextureKey>(c));
            m_grid.push_back(std::move(decoded));
        }
    }
    if (m_grid.empty())
    {
        if (!rootHasPolygonGeometry)
        {
            m_lastError = "Interior map is missing grid array and has no polygon geometry.";
            return false;
        }
        // Polygon-native maps do not need tile geometry. Keep one harmless
        // compatibility cell for older editor helpers and door code paths.
        m_grid = {{kNoTexture}};
        m_sectorGrid = {"A"};
    }

    if (root.contains("sector_codes") && root["sector_codes"].is_array())
    {
        for (const auto& row : root["sector_codes"])
        {
            if (!row.is_array())
                continue;
            std::string decoded;
            decoded.reserve(row.size());
            for (const auto& value : row)
            {
                const int code = std::clamp(value.get<int>(), 1, 255);
                decoded.push_back(static_cast<char>(static_cast<unsigned char>(code)));
            }
            m_sectorGrid.push_back(std::move(decoded));
        }
    }
    else if (root.contains("sector_grid") && root["sector_grid"].is_array())
    {
        for (const auto& row : root["sector_grid"])
        {
            std::string decoded = row.get<std::string>();
            for (char& symbol : decoded)
            {
                if (symbol == ' ' || symbol == '\0')
                    symbol = 'A';
            }
            m_sectorGrid.push_back(std::move(decoded));
        }
    }
    ensureSectorGrid();
    ensureDefaultSectors();

    if (root.contains("sectors") && root["sectors"].is_object())
    {
        for (auto it = root["sectors"].begin(); it != root["sectors"].end(); ++it)
        {
            if (it.key().empty()) continue;
            const char symbol = sectorSymbolFromKey(it.key());
            const json& source = it.value();
            SectorDef def = m_sectors.count(symbol) ? m_sectors[symbol] : SectorDef{};
            def.symbol = symbol;
            def.id = source.value("id", "sector_" + sectorKeyString(symbol));
            def.name = source.value("name", def.id);
            def.floorTexture = textureKeyFromJson(source, "floor_texture_key", "floor_texture", def.floorTexture);
            def.ceilingTexture = textureKeyFromJson(source, "ceiling_texture_key", "ceiling_texture", def.ceilingTexture);
            def.boundaryTexture = textureKeyFromJson(source, "boundary_texture_key", "boundary_texture", def.boundaryTexture);
            if (source.contains("floor_color"))
            {
                const json& c = source["floor_color"];
                def.floorColorR = jsonColor(c, 0, def.floorColorR);
                def.floorColorG = jsonColor(c, 1, def.floorColorG);
                def.floorColorB = jsonColor(c, 2, def.floorColorB);
            }
            if (source.contains("ceiling_color"))
            {
                const json& c = source["ceiling_color"];
                def.ceilingColorR = jsonColor(c, 0, def.ceilingColorR);
                def.ceilingColorG = jsonColor(c, 1, def.ceilingColorG);
                def.ceilingColorB = jsonColor(c, 2, def.ceilingColorB);
            }
            def.floorHeight = source.value("floor_height", def.floorHeight);
            def.ceilingHeight = source.value("ceiling_height", def.ceilingHeight);
            def.ambient = std::clamp(source.value("ambient", def.ambient), 0.15, 1.35);
            def.wallHeight = source.value("wall_height", def.wallHeight);
            if (def.wallHeight > 0.0)
                def.wallHeight = std::max(0.35, def.wallHeight);
            def.skyCeiling = source.value("sky_ceiling", def.skyCeiling);
            def.floorSlopeX = std::clamp(source.value("floor_slope_x", def.floorSlopeX), -2.0, 2.0);
            def.floorSlopeY = std::clamp(source.value("floor_slope_y", def.floorSlopeY), -2.0, 2.0);
            def.ceilingSlopeX = std::clamp(source.value("ceiling_slope_x", def.ceilingSlopeX), -2.0, 2.0);
            def.ceilingSlopeY = std::clamp(source.value("ceiling_slope_y", def.ceilingSlopeY), -2.0, 2.0);
            def.slopeOriginX = source.value("slope_origin_x", def.slopeOriginX);
            def.slopeOriginY = source.value("slope_origin_y", def.slopeOriginY);
            if (def.ceilingHeight < def.floorHeight + 0.35)
                def.ceilingHeight = def.floorHeight + 0.35;
            m_sectors[symbol] = def;
        }
    }
    rebuildSectorLookup();

    if (root.contains("textures") && root["textures"].is_array())
    {
        for (const auto& item : root["textures"])
        {
            if (!item.is_object()) continue;
            const TextureKey key = static_cast<TextureKey>(std::clamp(item.value("key", 0), 0, 65535));
            const std::string path = item.value("path", std::string());
            if (key != kNoTexture && !path.empty())
                loadTextureForCell(key, path);
        }
    }
    else if (root.contains("textures") && root["textures"].is_object())
    {
        for (auto it = root["textures"].begin(); it != root["textures"].end(); ++it)
        {
            if (it.key().empty()) continue;
            TextureKey key = 0;
            try { key = static_cast<TextureKey>(std::stoul(it.key())); }
            catch (...) { key = static_cast<std::uint8_t>(it.key().front()); }
            loadTextureForCell(key, it.value().get<std::string>());
        }
    }

    const std::vector<std::pair<TextureKey, std::string>> defaults = {
        {'1', "assets/Interior/wall_stone.png"},
        {'2', "assets/Interior/wall_plaster.png"},
        {'3', "assets/Interior/wall_brick.png"},
        {'4', "assets/Interior/wall_metal.png"},
        {'D', "assets/Interior/door_wood.png"},
        {'F', "assets/Interior/floor_boards.png"},
        {'S', "assets/Interior/floor_stone.png"},
        {'C', "assets/Interior/ceiling_beams.png"},
        {'P', "assets/Interior/ceiling_plaster.png"},
        {'K', "assets/Interior/sky_overcast.png"},
        {'T', "assets/Interior/sprite_torch.png"},
        {'B', "assets/Interior/sprite_barrel.png"},
        {'X', "assets/Interior/sprite_crate.png"},
        {'R', "assets/Interior/sprite_stairs.png"}
    };
    for (const auto& entry : defaults)
    {
        if (!m_textures.count(entry.first))
            loadTextureForCell(entry.first, entry.second);
    }

    if (root.contains("wall_segments") && root["wall_segments"].is_array())
    {
        for (const auto& source : root["wall_segments"])
        {
            if (!source.is_object()) continue;
            WallSegmentDef wall;
            wall.id = source.value("id", std::string("wall_segment_") + std::to_string(m_wallSegments.size()));
            wall.x0 = source.value("x0", 0.0);
            wall.y0 = source.value("y0", 0.0);
            wall.x1 = source.value("x1", 1.0);
            wall.y1 = source.value("y1", 0.0);
            wall.bottomZ = source.value("bottom_z", 0.0);
            wall.topZ = source.value("top_z", 3.0);
            wall.bottomZEnd = source.value("bottom_z_end", wall.bottomZ);
            wall.topZEnd = source.value("top_z_end", wall.topZ);
            if (source.contains("top_profile") &&
                source["top_profile"].is_array())
            {
                for (const auto& height : source["top_profile"])
                    if (height.is_number())
                        wall.topProfile.push_back(height.get<double>());
            }
            wall.texture = textureKeyFromJson(source, "texture_key", "texture", static_cast<TextureKey>('1'));
            wall.ambient = std::clamp(source.value("ambient", 1.0), 0.15, 1.35);
            wall.textureScale = std::clamp(source.value("texture_scale", 1.0), 0.05, 8.0);
            wall.textureUOffset = source.value("texture_u_offset", 0.0);
            wall.worldAlignedTexture =
                source.value("world_aligned_texture", false);
            wall.solid = source.value("solid", true);
            wall.twoSided = source.value("two_sided", true);
            const bool variableHeight =
                source.contains("bottom_z_end") ||
                source.contains("top_z_end") ||
                !wall.topProfile.empty();
            if (variableHeight)
            {
                wall.topZ = std::max(wall.topZ, wall.bottomZ);
                wall.topZEnd =
                    std::max(wall.topZEnd, wall.bottomZEnd);
            }
            else
            {
                if (wall.topZ < wall.bottomZ + 0.10)
                    wall.topZ = wall.bottomZ + 0.10;
                wall.topZEnd = wall.topZ;
                wall.bottomZEnd = wall.bottomZ;
            }
            if (std::hypot(wall.x1 - wall.x0, wall.y1 - wall.y0) > 0.05)
                m_wallSegments.push_back(wall);
        }
    }

    if (root.contains("polygon_sectors") && root["polygon_sectors"].is_array())
    {
        for (const auto& source : root["polygon_sectors"])
        {
            if (!source.is_object() || !source.contains("vertices") || !source["vertices"].is_array()) continue;
            PolygonSectorRegion region;
            region.id = source.value("id", std::string("polygon_sector_") + std::to_string(m_polygonSectors.size()));
            region.sector = sectorSymbolFromJson(source, "sector", "sector_code", 'A');
            region.boundarySolid = source.value("boundary_solid", true);
            region.cutsUnderlyingFloor =
                source.value("cuts_underlying_floor", false);
            region.hasSupportBottom =
                source.value("has_support_bottom", false);
            region.supportBottomZ =
                source.value("support_bottom_z", 0.0);
            region.wallTexture = textureKeyFromJson(source, "wall_texture_key", "wall_texture", kNoTexture);
            region.wallAmbient = std::clamp(source.value("wall_ambient", 1.0), 0.15, 1.35);
            region.wallTextureScale = std::clamp(source.value("wall_texture_scale", 1.0), 0.05, 8.0);
            for (const auto& vertex : source["vertices"])
            {
                if (!vertex.is_array() || vertex.size() < 2) continue;
                region.vertices.push_back({vertex[0].get<double>(), vertex[1].get<double>()});
            }
            if (source.contains("edge_openings") && source["edge_openings"].is_array())
            {
                for (const auto& openingSource : source["edge_openings"])
                {
                    if (!openingSource.is_object()) continue;
                    PolygonEdgeOpening opening;
                    opening.edge = openingSource.value("edge", 0);
                    opening.start = std::clamp(openingSource.value("start", 0.35), 0.0, 1.0);
                    opening.end = std::clamp(openingSource.value("end", 0.65), 0.0, 1.0);
                    if (opening.end < opening.start) std::swap(opening.start, opening.end);
                    opening.bottom = std::max(0.0, openingSource.value("bottom", 0.0));
                    opening.height = std::max(0.10, openingSource.value("height", 2.10));
                    region.openings.push_back(opening);
                }
            }
            if (region.vertices.size() >= 3)
            {
                updatePolygonBounds(region);
                if (region.cutsUnderlyingFloor)
                    m_floorCutoutSectors[
                        static_cast<unsigned char>(region.sector)] = true;
                m_polygonSectors.push_back(std::move(region));
            }
        }
        rebuildPolygonBoundaryWalls();
        rebuildVectorSectorLookup();
    }

    if (root.contains("traversal_ramps") &&
        root["traversal_ramps"].is_array())
    {
        for (const auto& source : root["traversal_ramps"])
        {
            if (!source.is_object() ||
                !source.contains("start") || !source["start"].is_array() ||
                !source.contains("end") || !source["end"].is_array() ||
                source["start"].size() < 2 || source["end"].size() < 2)
                continue;

            TraversalRampDef ramp;
            ramp.id = source.value(
                "id", std::string("traversal_ramp_") +
                    std::to_string(m_traversalRamps.size()));
            ramp.startX = source["start"][0].get<double>();
            ramp.startY = source["start"][1].get<double>();
            ramp.endX = source["end"][0].get<double>();
            ramp.endY = source["end"][1].get<double>();
            ramp.startHeight = source.value("start_height", 0.0);
            ramp.endHeight = source.value("end_height", ramp.startHeight);
            ramp.width = std::clamp(source.value("width", 1.0), 0.35, 12.0);
            if (std::hypot(
                    ramp.endX - ramp.startX,
                    ramp.endY - ramp.startY) > 0.10)
                m_traversalRamps.push_back(std::move(ramp));
        }
    }

    if (root.contains("doors") && root["doors"].is_array())
    {
        for (const auto& source : root["doors"])
        {
            DoorDef door;
            door.x = source.value("x", 0);
            door.y = source.value("y", 0);
            door.id = source.value("id", makeDoorId(door.x, door.y));
            door.locked = source.value("locked", false);
            door.targetOpen = source.value("open", source.value("initial_open", false));
            door.openAmount = door.targetOpen ? 1.0 : 0.0;
            door.speed = std::clamp(source.value("speed", 1.8), 0.15, 8.0);
            door.targetInterior = source.value("target_interior", std::string());
            door.targetSpawn = source.value("target_spawn", std::string());
            door.motion = parseDoorMotion(source.value(
                "motion", door.targetInterior.empty() ? std::string("swing") : std::string("transition")));
            door.texture = textureKeyFromJson(source, "texture_key", "texture", static_cast<TextureKey>('D'));
            door.span = std::clamp(source.value("span", 1), 1, 16);
            door.axis = firstCharOr(source.value("axis", std::string("x")), 'x');
            if (door.axis != 'x' && door.axis != 'y') door.axis = 'x';
            door.height = source.value("height", -1.0);
            const std::string hinge = source.value("hinge", std::string("start"));
            door.hingeAtEnd = hinge == "end" || hinge == "right" || hinge == "high";
            door.swingDirection = source.value("swing_direction", 1.0) < 0.0 ? -1.0 : 1.0;
            door.swingDegrees = std::clamp(source.value("swing_degrees", 90.0), 15.0, 170.0);
            door.thickness = std::clamp(source.value("thickness", 0.08), 0.02, 0.30);
            door.interactionGroup = source.value("interaction_group", door.id);
            door.textureU0 = std::clamp(source.value("texture_u0", 0.0), 0.0, 1.0);
            door.textureU1 = std::clamp(source.value("texture_u1", 1.0), 0.0, 1.0);
            if (source.contains("segment") && source["segment"].is_array() &&
                source["segment"].size() >= 2 &&
                source["segment"][0].is_array() && source["segment"][0].size() >= 2 &&
                source["segment"][1].is_array() && source["segment"][1].size() >= 2)
            {
                door.segmentX0 = source["segment"][0][0].get<double>();
                door.segmentY0 = source["segment"][0][1].get<double>();
                door.segmentX1 = source["segment"][1][0].get<double>();
                door.segmentY1 = source["segment"][1][1].get<double>();
                door.hasSegment = std::isfinite(door.segmentX0) &&
                                  std::isfinite(door.segmentY0) &&
                                  std::isfinite(door.segmentX1) &&
                                  std::isfinite(door.segmentY1) &&
                                  std::hypot(door.segmentX1 - door.segmentX0,
                                             door.segmentY1 - door.segmentY0) > 0.05;
            }

            bool valid = door.hasSegment;
            if (!door.hasSegment)
            {
                valid = true;
                for (int i = 0; i < door.span; ++i)
                {
                    const int cellX = door.x + (door.axis == 'x' ? i : 0);
                    const int cellY = door.y + (door.axis == 'y' ? i : 0);
                    if (!isInside(cellX, cellY))
                    {
                        valid = false;
                        break;
                    }
                }
            }
            if (valid)
            {
                if (!door.hasSegment)
                {
                    for (int i = 0; i < door.span; ++i)
                    {
                        const int cellX = door.x + (door.axis == 'x' ? i : 0);
                        const int cellY = door.y + (door.axis == 'y' ? i : 0);
                        m_grid[cellY][cellX] = kNoTexture;
                    }
                }
                m_doors.push_back(door);
            }
        }
    }
    rebuildDoorsFromGridIfMissing();

    if (root.contains("sprites") && root["sprites"].is_array())
    {
        for (const auto& source : root["sprites"])
        {
            SpriteDef sprite;
            sprite.id = source.value("id", makeSpriteId());
            sprite.texture = textureKeyFromJson(source, "texture_key", "texture", static_cast<TextureKey>('T'));
            sprite.x = source.value("x", 2.5);
            sprite.y = source.value("y", 2.5);
            sprite.zOffset = source.value("z_offset", 0.0);
            sprite.scale = std::clamp(source.value("scale", 0.75), 0.10, 4.0);
            sprite.solid = source.value("solid", false);
            const bool prefersBillboard = sprite.id.find("marker") != std::string::npos ||
                sprite.id.find("portal") != std::string::npos ||
                sprite.id.find("lid") != std::string::npos;
            const std::string renderMode = source.value(
                "render_mode", sprite.solid && !prefersBillboard
                    ? std::string("voxel")
                    : std::string("billboard"));
            sprite.renderMode = renderMode == "voxel" || renderMode == "voxels" || renderMode == "3d"
                ? ObjectRenderMode::Voxel
                : ObjectRenderMode::Billboard;
            sprite.yaw = source.value("yaw_degrees", 0.0) * kPi / 180.0;
            sprite.voxelDepth = std::clamp(source.value("voxel_depth", 0.0), 0.0, 3.0);

            const TextureRef* textureRef = m_textureLookup[static_cast<std::size_t>(sprite.texture)];
            std::string texturePath;
            const auto texturePathIt = m_texturePaths.find(sprite.texture);
            if (texturePathIt != m_texturePaths.end()) texturePath = lowerAscii(texturePathIt->second);
            const std::string lowerId = lowerAscii(sprite.id);
            const bool fireLike = lowerId.find("torch") != std::string::npos ||
                                  lowerId.find("fire") != std::string::npos ||
                                  lowerId.find("ohn") != std::string::npos ||
                                  texturePath.find("torch") != std::string::npos ||
                                  texturePath.find("fire") != std::string::npos ||
                                  texturePath.find("campfire") != std::string::npos;
            const bool hasAnimationFrames = textureRef && !textureRef->animationFrames.empty();
            sprite.animated = source.value("animated", hasAnimationFrames || fireLike);
            sprite.randomAnimation = source.value("random_animation", true);
            sprite.animationMinFps = std::clamp(source.value("animation_min_fps", 7.0), 1.0, 30.0);
            sprite.animationMaxFps = std::clamp(source.value("animation_max_fps", 12.0),
                                                sprite.animationMinFps, 40.0);
            sprite.animationSeed = stableSeed(sprite.id + texturePath);
            sprite.animationTimer = 0.015 + randomUnit(sprite.animationSeed) * 0.12;

            sprite.emitsLight = source.value("emits_light", fireLike);
            if (source.contains("light_color"))
            {
                const json& c = source["light_color"];
                sprite.lightR = jsonColor(c, 0, sprite.lightR);
                sprite.lightG = jsonColor(c, 1, sprite.lightG);
                sprite.lightB = jsonColor(c, 2, sprite.lightB);
            }
            sprite.lightRadius = std::clamp(source.value("light_radius", fireLike ? 3.4 : 0.0), 0.0, 12.0);
            sprite.lightIntensity = std::clamp(source.value("light_intensity", fireLike ? 0.95 : 0.0), 0.0, 3.0);
            sprite.lightHeight = std::clamp(source.value("light_height", fireLike ? sprite.scale * 0.68 : 0.0),
                                            -1.0, 5.0);
            sprite.lightFlicker = std::clamp(source.value("light_flicker", fireLike ? 0.20 : 0.0), 0.0, 0.75);
            sprite.lightMultiplier = 1.0;

            sprite.interactionLabel = source.value("interaction_label", std::string());
            sprite.targetInterior = source.value("target_interior", std::string());
            sprite.targetSpawn = source.value("target_spawn", std::string());
            m_sprites.push_back(sprite);
        }
    }

    prepareTextureModels();
    // Polygon loading above already built this table. Keep the fallback for
    // older/generated maps whose geometry was appended by another loader,
    // without paying for the complete courtyard lookup a second time.
    if (m_vectorSectorLookup.empty() && !m_polygonSectors.empty())
        rebuildVectorSectorLookup();

    if (!canStandAt(m_posX, m_posY))
    {
        bool found = false;
        if (hasVectorGeometry())
        {
            // Polygon maps are not required to have meaningful legacy tiles.
            // Try the centroid of each authored region instead of scanning the
            // square grid for an empty cell.
            for (const PolygonSectorRegion& region : m_polygonSectors)
            {
                if (region.vertices.empty()) continue;
                double cx = 0.0;
                double cy = 0.0;
                for (const auto& vertex : region.vertices)
                {
                    cx += vertex[0];
                    cy += vertex[1];
                }
                cx /= static_cast<double>(region.vertices.size());
                cy /= static_cast<double>(region.vertices.size());
                if (canStandAt(cx, cy))
                {
                    m_posX = cx;
                    m_posY = cy;
                    found = true;
                    break;
                }
            }
        }
        else
        {
            for (int y = 0; y < static_cast<int>(m_grid.size()) && !found; ++y)
            {
                for (int x = 0; x < static_cast<int>(m_grid[y].size()); ++x)
                {
                    if (isEmptyCell(cellAt(x, y)))
                    {
                        m_posX = x + 0.5;
                        m_posY = y + 0.5;
                        found = true;
                        break;
                    }
                }
            }
        }
    }

    m_playerZ = floorHeightAtWorld(m_posX, m_posY);
    m_verticalVelocity = 0.0;
    m_grounded = true;
    m_playerZInitialized = true;
    m_cameraFloorZ = m_playerZ;
    m_cameraFloorInitialized = true;
    m_lastSafeX = m_posX;
    m_lastSafeY = m_posY;
    sanitizeEditorState();

    m_status = "Loaded interior: " + m_displayName;
    m_sceneDirty = true;
    return true;
}

bool BuildInteriorEngine::saveInterior(const std::string& interiorIdOrPath)
{
    // Castle maps are authored in data/castles/*/maps. Keep that source JSON
    // authoritative, but also refresh the compiled preview so a reload inside
    // the editor does not bring back stale texture keys.
    if (interiorIdOrPath.empty() &&
        !m_loadedCastleId.empty() && !m_loadedCastleMapId.empty())
    {
        const bool savedSectors = saveCastleMapSectors();
        if (!savedSectors)
            return false;
        if (!saveCastleEntityOverlay())
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Castle entity overlay was not saved after sector save: %s",
                        m_lastError.c_str());
        m_lastError.clear();
        m_status = U8("Uložena zdrojová mapa hradu.");
        return true;
    }

    const std::string path = interiorIdOrPath.empty() ? m_loadedPath : resolveInteriorPath(interiorIdOrPath);
    if (path.empty())
    {
        m_lastError = "No interior path to save.";
        return false;
    }

    try { fs::create_directories(fs::path(path).parent_path()); }
    catch (...) {}

    json root;
    root["id"] = m_currentInteriorId;
    root["name"] = m_displayName;
    root["fov_degrees"] = m_fov * 180.0 / kPi;
    root["eye_height"] = m_eyeHeight;
    root["spawn"] = {
        {"x", m_posX},
        {"y", m_posY},
        {"angle_degrees", m_angle * 180.0 / kPi},
        {"pitch", m_pitch}
    };
    json namedSpawns = json::object();
    for (const auto& pair : m_namedSpawns)
    {
        const SpawnDef& spawn = pair.second;
        namedSpawns[pair.first] = {
            {"x", spawn.x}, {"y", spawn.y},
            {"angle_degrees", spawn.angle * 180.0 / kPi},
            {"pitch", spawn.pitch}
        };
    }
    root["spawns"] = namedSpawns;

    json textures = json::array();
    for (const auto& pair : m_texturePaths)
        textures.push_back({{"key", pair.first}, {"path", pair.second}});
    root["textures"] = std::move(textures);
    root["grid_codes"] = m_grid;
    json sectorCodes = json::array();
    bool canWriteLegacySectorGrid = true;
    for (const std::string& row : m_sectorGrid)
    {
        json codeRow = json::array();
        for (unsigned char symbol : row)
        {
            codeRow.push_back(static_cast<int>(symbol));
            if (symbol < 33 || symbol > 126)
                canWriteLegacySectorGrid = false;
        }
        sectorCodes.push_back(std::move(codeRow));
    }
    root["sector_codes"] = std::move(sectorCodes);
    if (canWriteLegacySectorGrid)
        root["sector_grid"] = m_sectorGrid;

    json sectors = json::object();
    for (const auto& pair : m_sectors)
    {
        const SectorDef& s = pair.second;
        sectors[sectorKeyString(s.symbol)] = {
            {"id", s.id},
            {"name", s.name},
            {"code", sectorCode(s.symbol)},
            {"floor_texture_key", s.floorTexture},
            {"ceiling_texture_key", s.ceilingTexture},
            {"boundary_texture_key", s.boundaryTexture},
            {"floor_color", {s.floorColorR, s.floorColorG, s.floorColorB}},
            {"ceiling_color", {s.ceilingColorR, s.ceilingColorG, s.ceilingColorB}},
            {"floor_height", s.floorHeight},
            {"ceiling_height", s.ceilingHeight},
            {"ambient", s.ambient},
            {"wall_height", s.wallHeight},
            {"sky_ceiling", s.skyCeiling},
            {"floor_slope_x", s.floorSlopeX},
            {"floor_slope_y", s.floorSlopeY},
            {"ceiling_slope_x", s.ceilingSlopeX},
            {"ceiling_slope_y", s.ceilingSlopeY},
            {"slope_origin_x", s.slopeOriginX},
            {"slope_origin_y", s.slopeOriginY}
        };
    }
    root["sectors"] = sectors;

    json doors = json::array();
    for (const DoorDef& d : m_doors)
    {
        json doorJson = {
            {"id", d.id.empty() ? makeDoorId(d.x, d.y) : d.id},
            {"x", d.x}, {"y", d.y},
            {"locked", d.locked},
            {"open", d.targetOpen},
            {"speed", d.speed},
            {"motion", doorMotionName(d.motion)},
            {"texture_key", d.texture},
            {"span", d.span},
            {"axis", std::string(1, d.axis)},
            {"height", d.height},
            {"hinge", d.hingeAtEnd ? "end" : "start"},
            {"swing_direction", d.swingDirection},
            {"swing_degrees", d.swingDegrees},
            {"thickness", d.thickness},
            {"interaction_group", d.interactionGroup.empty() ? d.id : d.interactionGroup},
            {"texture_u0", d.textureU0},
            {"texture_u1", d.textureU1},
            {"target_interior", d.targetInterior},
            {"target_spawn", d.targetSpawn}
        };
        if (d.hasSegment)
        {
            doorJson["segment"] = {
                {d.segmentX0, d.segmentY0},
                {d.segmentX1, d.segmentY1}
            };
        }
        doors.push_back(std::move(doorJson));
    }
    root["doors"] = doors;

    json wallSegments = json::array();
    for (const WallSegmentDef& wall : m_wallSegments)
    {
        wallSegments.push_back({
            {"id", wall.id},
            {"x0", wall.x0}, {"y0", wall.y0},
            {"x1", wall.x1}, {"y1", wall.y1},
            {"bottom_z", wall.bottomZ}, {"top_z", wall.topZ},
            {"bottom_z_end", wall.bottomZEnd},
            {"top_z_end", wall.topZEnd},
            {"texture_key", wall.texture},
            {"ambient", wall.ambient},
            {"texture_scale", wall.textureScale},
            {"texture_u_offset", wall.textureUOffset},
            {"world_aligned_texture", wall.worldAlignedTexture},
            {"solid", wall.solid},
            {"two_sided", wall.twoSided}
        });
        if (!wall.topProfile.empty())
            wallSegments.back()["top_profile"] = wall.topProfile;
    }
    root["wall_segments"] = wallSegments;

    json polygonSectors = json::array();
    for (const PolygonSectorRegion& region : m_polygonSectors)
    {
        json vertices = json::array();
        for (const auto& vertex : region.vertices) vertices.push_back({vertex[0], vertex[1]});
        json openings = json::array();
        for (const PolygonEdgeOpening& opening : region.openings)
        {
            openings.push_back({
                {"edge", opening.edge}, {"start", opening.start}, {"end", opening.end},
                {"bottom", opening.bottom}, {"height", opening.height}
            });
        }
        polygonSectors.push_back({
            {"id", region.id},
            {"sector", sectorKeyString(region.sector)},
            {"sector_code", sectorCode(region.sector)},
            {"boundary_solid", region.boundarySolid},
            {"cuts_underlying_floor", region.cutsUnderlyingFloor},
            {"has_support_bottom", region.hasSupportBottom},
            {"support_bottom_z", region.supportBottomZ},
            {"wall_texture_key", region.wallTexture},
            {"wall_ambient", region.wallAmbient},
            {"wall_texture_scale", region.wallTextureScale},
            {"edge_openings", std::move(openings)},
            {"vertices", std::move(vertices)}
        });
    }
    root["polygon_sectors"] = polygonSectors;

    json traversalRamps = json::array();
    for (const TraversalRampDef& ramp : m_traversalRamps)
    {
        traversalRamps.push_back({
            {"id", ramp.id},
            {"start", {ramp.startX, ramp.startY}},
            {"end", {ramp.endX, ramp.endY}},
            {"start_height", ramp.startHeight},
            {"end_height", ramp.endHeight},
            {"width", ramp.width}
        });
    }
    root["traversal_ramps"] = std::move(traversalRamps);

    json sprites = json::array();
    for (const SpriteDef& s : m_sprites)
    {
        sprites.push_back({
            {"id", s.id},
            {"texture_key", s.texture},
            {"x", s.x}, {"y", s.y},
            {"z_offset", s.zOffset},
            {"scale", s.scale},
            {"solid", s.solid},
            {"render_mode", s.renderMode == ObjectRenderMode::Voxel ? "voxel" : "billboard"},
            {"yaw_degrees", s.yaw * 180.0 / kPi},
            {"voxel_depth", s.voxelDepth},
            {"animated", s.animated},
            {"random_animation", s.randomAnimation},
            {"animation_min_fps", s.animationMinFps},
            {"animation_max_fps", s.animationMaxFps},
            {"emits_light", s.emitsLight},
            {"light_color", {s.lightR, s.lightG, s.lightB}},
            {"light_radius", s.lightRadius},
            {"light_intensity", s.lightIntensity},
            {"light_height", s.lightHeight},
            {"light_flicker", s.lightFlicker},
            {"interaction_label", s.interactionLabel},
            {"target_interior", s.targetInterior},
            {"target_spawn", s.targetSpawn}
        });
    }
    root["sprites"] = sprites;

    std::ofstream out(path);
    if (!out)
    {
        m_lastError = "Cannot save interior: " + path;
        return false;
    }
    out << root.dump(2);
    m_loadedPath = path;
    m_status = "Saved interior: " + path;
    return true;
}

bool BuildInteriorEngine::saveCastleMapSectors()
{
    if (m_loadedCastleId.empty() || m_loadedCastleMapId.empty())
    {
        m_lastError = "Sectors can only be saved for a castle: map.";
        m_status = m_lastError;
        return false;
    }

    const fs::path castleRoot = ProjectRootPath() / "data" / "castles" / m_loadedCastleId;
    const fs::path mapPath = castleRoot / "maps" / (m_loadedCastleMapId + ".map.json");
    const fs::path materialsPath = castleRoot / "materials.json";

    json mapRoot;
    json materialsRoot;
    try
    {
        std::ifstream mapIn(mapPath);
        if (!mapIn)
        {
            m_lastError = "Cannot open castle source map: " + mapPath.string();
            m_status = m_lastError;
            return false;
        }
        mapIn >> mapRoot;

        std::ifstream materialsIn(materialsPath);
        if (materialsIn)
            materialsIn >> materialsRoot;
        if (!materialsRoot.is_object())
            materialsRoot = json::object();
        if (!materialsRoot.contains("materials") || !materialsRoot["materials"].is_array())
            materialsRoot["materials"] = json::array();
    }
    catch (const std::exception& ex)
    {
        m_lastError = std::string("Castle source save parse failed: ") + ex.what();
        m_status = m_lastError;
        return false;
    }

    auto normalizedPath = [](const std::string& value) {
        return lowerAscii(fs::path(value).lexically_normal().generic_string());
    };

    auto materialLookupKey = [](const std::string& normalizedTexturePath,
                                const std::string& kind)
    {
        return normalizedTexturePath + "|" + lowerAscii(kind);
    };

    std::unordered_map<std::string, std::string> materialByTextureAndKind;
    std::unordered_set<std::string> usedMaterialIds;
    for (const json& material : materialsRoot["materials"])
    {
        if (!material.is_object()) continue;
        const std::string id = material.value("id", std::string());
        const std::string texture = material.value("texture", std::string());
        const std::string kind = material.value("kind", std::string("surface"));
        if (!id.empty()) usedMaterialIds.insert(id);
        if (!id.empty() && !texture.empty())
            materialByTextureAndKind[
                materialLookupKey(normalizedPath(texture), kind)] = id;
    }

    auto sanitizeMaterialIdPart = [](std::string value)
    {
        std::string out;
        out.reserve(value.size());
        bool lastUnderscore = false;
        for (unsigned char c : value)
        {
            if (std::isalnum(c))
            {
                out.push_back(static_cast<char>(std::tolower(c)));
                lastUnderscore = false;
            }
            else if (!lastUnderscore)
            {
                out.push_back('_');
                lastUnderscore = true;
            }
        }
        while (!out.empty() && out.front() == '_') out.erase(out.begin());
        while (!out.empty() && out.back() == '_') out.pop_back();
        return out.empty() ? std::string("texture") : out;
    };

    bool materialsChanged = false;
    auto materialForTextureKey = [&](TextureKey key, const char* kind) -> std::string
    {
        const std::string kindText = kind ? kind : "surface";
        const auto textureIt = m_texturePaths.find(key);
        if (textureIt == m_texturePaths.end() || textureIt->second.empty())
            return {};

        const std::string texturePath = fs::path(textureIt->second).lexically_normal().generic_string();
        const std::string normalized = normalizedPath(texturePath);
        const auto materialIt =
            materialByTextureAndKind.find(materialLookupKey(normalized, kindText));
        if (materialIt != materialByTextureAndKind.end())
            return materialIt->second;

        fs::path path(texturePath);
        std::string baseId = "editor_" +
            sanitizeMaterialIdPart(path.parent_path().filename().generic_string()) + "_" +
            sanitizeMaterialIdPart(path.stem().generic_string());
        if (!kindText.empty())
            baseId += "_" + sanitizeMaterialIdPart(kindText);
        std::string materialId = baseId;
        int suffix = 2;
        while (usedMaterialIds.find(materialId) != usedMaterialIds.end())
            materialId = baseId + "_" + std::to_string(suffix++);

        materialsRoot["materials"].push_back({
            {"id", materialId},
            {"kind", kindText},
            {"texture", texturePath}
        });
        usedMaterialIds.insert(materialId);
        materialByTextureAndKind[materialLookupKey(normalized, kindText)] = materialId;
        materialsChanged = true;
        return materialId;
    };

    int savedRooms = 0;
    std::unordered_map<std::string, const PolygonSectorRegion*> polygonByRoomId;
    for (const PolygonSectorRegion& region : m_polygonSectors)
        if (!region.id.empty())
            polygonByRoomId[region.id] = &region;

    std::unordered_map<std::string, std::vector<std::string>> stairFloorMaterials;
    std::unordered_map<std::string, std::vector<std::string>> stairBoundaryMaterials;
    auto parentStairId = [](const std::string& sectorId) -> std::string
    {
        const std::string marker = "_step_";
        const std::size_t markerPos = sectorId.find(marker);
        if (markerPos == std::string::npos)
            return {};
        return sectorId.substr(0, markerPos);
    };
    auto writeSectorSettings = [&](json& target,
                                   const SectorDef& sector,
                                   const std::string& id,
                                   bool polygonRoom)
    {
        if (polygonRoom)
            target["name"] = sector.name;
        else
            target["display_name"] = sector.name;
        target["ambient"] = sector.ambient;
        target["wall_height"] = sector.wallHeight;
        target["sky_ceiling"] = sector.skyCeiling;

        if (!target.contains("floor") || !target["floor"].is_object())
            target["floor"] = json::object();
        target["floor"]["height"] = sector.floorHeight;
        target["floor"]["slope_x"] = sector.floorSlopeX;
        target["floor"]["slope_y"] = sector.floorSlopeY;
        target["floor"]["origin_x"] = sector.slopeOriginX;
        target["floor"]["origin_y"] = sector.slopeOriginY;
        const std::string floorMaterial = materialForTextureKey(sector.floorTexture, "floor");
        if (!floorMaterial.empty())
            target["floor"]["material"] = floorMaterial;

        if (!target.contains("ceiling") || !target["ceiling"].is_object())
            target["ceiling"] = json::object();
        target["ceiling"]["height"] = sector.ceilingHeight;
        target["ceiling"]["slope_x"] = sector.ceilingSlopeX;
        target["ceiling"]["slope_y"] = sector.ceilingSlopeY;
        target["ceiling"]["origin_x"] = sector.slopeOriginX;
        target["ceiling"]["origin_y"] = sector.slopeOriginY;
        if (sector.skyCeiling)
            target["ceiling"]["type"] = "sky";
        const std::string ceilingMaterial = materialForTextureKey(sector.ceilingTexture, "ceiling");
        if (!ceilingMaterial.empty())
            target["ceiling"]["material"] = ceilingMaterial;

        const std::string wallMaterial = materialForTextureKey(sector.boundaryTexture, "wall");
        if (!wallMaterial.empty())
            target["boundary_material"] = wallMaterial;
        const auto polygonIt = polygonByRoomId.find(id);
        if (polygonRoom &&
            polygonIt != polygonByRoomId.end() &&
            polygonIt->second->wallTexture != kNoTexture)
        {
            const std::string polygonWallMaterial =
                materialForTextureKey(polygonIt->second->wallTexture, "wall");
            if (!polygonWallMaterial.empty())
                target["boundary_material"] = polygonWallMaterial;
            target["ambient"] = polygonIt->second->wallAmbient;
        }
    };
    if (mapRoot.contains("rooms") && mapRoot["rooms"].is_array())
    {
        for (json& room : mapRoot["rooms"])
        {
            if (!room.is_object()) continue;
            const std::string id = room.value("id", std::string());
            auto sectorIt = std::find_if(m_sectors.begin(), m_sectors.end(),
                [&](const auto& pair) { return pair.second.id == id; });
            if (sectorIt == m_sectors.end())
                continue;

            const SectorDef& sector = sectorIt->second;
            writeSectorSettings(room, sector, id, true);

            ++savedRooms;
        }
    }
    if (mapRoot.contains("sectors") && mapRoot["sectors"].is_array())
    {
        for (json& sectorJson : mapRoot["sectors"])
        {
            if (!sectorJson.is_object()) continue;
            const std::string id = sectorJson.value("id", std::string());
            auto sectorIt = std::find_if(m_sectors.begin(), m_sectors.end(),
                [&](const auto& pair) { return pair.second.id == id; });
            if (sectorIt == m_sectors.end())
                continue;

            writeSectorSettings(sectorJson, sectorIt->second, id, false);
            ++savedRooms;
        }
    }

    for (const auto& pair : m_sectors)
    {
        const SectorDef& sector = pair.second;
        const std::string stairId = parentStairId(sector.id);
        if (stairId.empty())
            continue;
        const std::string floorMaterial = materialForTextureKey(sector.floorTexture, "floor");
        if (!floorMaterial.empty())
            stairFloorMaterials[stairId].push_back(floorMaterial);
        const std::string boundaryMaterial = materialForTextureKey(sector.boundaryTexture, "wall");
        if (!boundaryMaterial.empty())
            stairBoundaryMaterials[stairId].push_back(boundaryMaterial);
    }

    if (mapRoot.contains("stairs") && mapRoot["stairs"].is_array())
    {
        auto chooseEditedMaterial = [](const std::vector<std::string>& values,
                                       const std::string& current) -> std::string
        {
            for (const std::string& value : values)
                if (!value.empty() && value != current)
                    return value;
            for (const std::string& value : values)
                if (!value.empty())
                    return value;
            return {};
        };

        for (json& stair : mapRoot["stairs"])
        {
            if (!stair.is_object()) continue;
            const std::string id = stair.value("id", std::string());
            const auto floorIt = stairFloorMaterials.find(id);
            if (floorIt != stairFloorMaterials.end())
            {
                const std::string selected = chooseEditedMaterial(
                    floorIt->second,
                    stair.value("floor_material", std::string()));
                if (!selected.empty())
                    stair["floor_material"] = selected;
            }
            const auto boundaryIt = stairBoundaryMaterials.find(id);
            if (boundaryIt != stairBoundaryMaterials.end())
            {
                const std::string selected = chooseEditedMaterial(
                    boundaryIt->second,
                    stair.value("boundary_material", std::string()));
                if (!selected.empty())
                    stair["boundary_material"] = selected;
            }
        }
    }
    auto writeSpawnJson = [&](json& target,
                              const std::string& id,
                              const SpawnDef& spawn)
    {
        if (!id.empty())
            target["id"] = id;
        if (target.contains("position") && target["position"].is_array())
        {
            target["position"] = {spawn.x, spawn.y};
        }
        else
        {
            target["x"] = spawn.x;
            target["y"] = spawn.y;
        }
        target["angle_degrees"] = spawn.angle * 180.0 / kPi;
        target["pitch"] = spawn.pitch;
    };
    auto currentCameraSpawn = [&]()
    {
        SpawnDef spawn;
        spawn.x = m_posX;
        spawn.y = m_posY;
        spawn.angle = m_angle;
        spawn.pitch = m_pitch;
        return spawn;
    };

    if (!mapRoot.contains("spawns") || !mapRoot["spawns"].is_array())
        mapRoot["spawns"] = json::array();
    json& sourceSpawns = mapRoot["spawns"];
    std::unordered_set<std::string> writtenSpawnIds;
    if (sourceSpawns.empty())
    {
        json sourceSpawn = json::object();
        writeSpawnJson(sourceSpawn, "entry", currentCameraSpawn());
        sourceSpawns.push_back(std::move(sourceSpawn));
        writtenSpawnIds.insert("entry");
    }
    else
    {
        json& defaultSpawn = sourceSpawns[0];
        if (!defaultSpawn.is_object())
            defaultSpawn = json::object();
        std::string defaultSpawnId = defaultSpawn.value("id", std::string("entry"));
        if (defaultSpawnId.empty())
            defaultSpawnId = "entry";
        const auto defaultNamedIt = m_namedSpawns.find(defaultSpawnId);
        if (m_editorDefaultSpawnDirty)
        {
            writeSpawnJson(defaultSpawn, defaultSpawnId, currentCameraSpawn());
        }
        else if (defaultNamedIt != m_namedSpawns.end())
        {
            writeSpawnJson(defaultSpawn, defaultSpawnId, defaultNamedIt->second);
        }
        writtenSpawnIds.insert(defaultSpawnId);
    }

    for (json& sourceSpawn : sourceSpawns)
    {
        if (!sourceSpawn.is_object())
            continue;
        const std::string id = sourceSpawn.value("id", std::string());
        if (id.empty() || writtenSpawnIds.find(id) != writtenSpawnIds.end())
            continue;
        const auto spawnIt = m_namedSpawns.find(id);
        if (spawnIt == m_namedSpawns.end())
            continue;
        writeSpawnJson(sourceSpawn, id, spawnIt->second);
        writtenSpawnIds.insert(id);
    }
    for (const auto& pair : m_namedSpawns)
    {
        if (pair.first.empty() ||
            writtenSpawnIds.find(pair.first) != writtenSpawnIds.end())
            continue;
        json sourceSpawn = json::object();
        writeSpawnJson(sourceSpawn, pair.first, pair.second);
        sourceSpawns.push_back(std::move(sourceSpawn));
        writtenSpawnIds.insert(pair.first);
    }

    if (savedRooms == 0)
    {
        m_lastError = "No matching castle rooms found for current sectors.";
        m_status = m_lastError;
        return false;
    }

    try
    {
        if (materialsChanged)
        {
            std::ofstream materialsOut(materialsPath);
            if (!materialsOut)
            {
                m_lastError = "Cannot save castle materials: " + materialsPath.string();
                m_status = m_lastError;
                return false;
            }
            materialsOut << materialsRoot.dump(2) << '\n';
        }

        std::ofstream mapOut(mapPath);
        if (!mapOut)
        {
            m_lastError = "Cannot save castle source map: " + mapPath.string();
            m_status = m_lastError;
            return false;
        }
        mapOut << mapRoot.dump(2) << '\n';

        const fs::path compiledPath =
            castleRoot / ".compiled" / (m_loadedCastleMapId + ".json");
        if (!saveInterior(compiledPath.string()))
            return false;
    }
    catch (const std::exception& ex)
    {
        m_lastError = std::string("Castle source save failed: ") + ex.what();
        m_status = m_lastError;
        return false;
    }

    m_lastError.clear();
    m_status = U8("Sektorové nastavení bylo uloženo do zdrojové mapy hradu.");
    m_editorDefaultSpawnDirty = false;
    return true;
}


bool BuildInteriorEngine::loadCastleEntityOverlay(const std::string& castleId,
                                                    const std::string& mapId)
{
    const fs::path castleRoot = ProjectRootPath() / "data" / "castles" / castleId;
    const fs::path entityPath = castleRoot / "entities" / (mapId + ".entities.json");
    std::error_code existsError;
    if (!fs::exists(entityPath, existsError) || existsError)
        return false;

    json entityRoot;
    try
    {
        std::ifstream entityIn(entityPath);
        if (!entityIn) return false;
        entityIn >> entityRoot;
    }
    catch (const std::exception& ex)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Castle entity overlay parse failed for %s: %s",
                    entityPath.string().c_str(), ex.what());
        return false;
    }

    if (!entityRoot.contains("entities") || !entityRoot["entities"].is_array())
        return false;

    std::unordered_map<std::string, std::string> materialTextures;
    const fs::path materialsPath = castleRoot / "materials.json";
    try
    {
        std::ifstream materialsIn(materialsPath);
        if (materialsIn)
        {
            json materialsRoot;
            materialsIn >> materialsRoot;
            if (materialsRoot.contains("materials") && materialsRoot["materials"].is_array())
            {
                for (const json& material : materialsRoot["materials"])
                {
                    if (!material.is_object()) continue;
                    const std::string id = material.value("id", std::string());
                    const std::string texture = material.value("texture", std::string());
                    if (!id.empty() && !texture.empty())
                        materialTextures[id] = texture;
                }
            }
        }
    }
    catch (const std::exception& ex)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Castle materials overlay parse failed for %s: %s",
                    materialsPath.string().c_str(), ex.what());
    }

    auto normalizedPath = [](const std::string& value) {
        return lowerAscii(fs::path(value).lexically_normal().generic_string());
    };

    auto findTextureKey = [&](const std::string& relativePath) -> TextureKey
    {
        if (relativePath.empty()) return static_cast<TextureKey>('T');
        const std::string wanted = normalizedPath(relativePath);
        for (const auto& pair : m_texturePaths)
        {
            if (normalizedPath(pair.second) == wanted)
                return pair.first;
        }
        TextureKey key = 256;
        for (const auto& pair : m_textures)
            if (pair.first >= key && pair.first < 65535) key = static_cast<TextureKey>(pair.first + 1);
        while (key != 0 && m_textures.find(key) != m_textures.end()) ++key;
        if (key != 0 && loadTextureForCell(key, relativePath))
            return key;
        return static_cast<TextureKey>('T');
    };

    int added = 0;
    for (const json& source : entityRoot["entities"])
    {
        if (!source.is_object()) continue;
        const std::string id = source.value("id", makeSpriteId());
        auto existingIt = std::find_if(m_sprites.begin(), m_sprites.end(),
            [&](const SpriteDef& sprite) { return sprite.id == id; });

        const std::string materialId = source.value(
            "material", source.value("prototype", std::string()));
        std::string texturePath = source.value("texture", std::string());
        const auto materialIt = materialTextures.find(materialId);
        if (materialIt != materialTextures.end())
            texturePath = materialIt->second;

        SpriteDef sprite;
        sprite.id = id;
        sprite.texture = findTextureKey(texturePath);
        sprite.x = source.value("x", 2.5);
        sprite.y = source.value("y", 2.5);
        sprite.zOffset = source.value("z_offset", 0.0);
        sprite.scale = std::clamp(source.value("scale", 0.75), 0.10, 4.0);
        sprite.solid = source.value("solid", false);

        const bool prefersBillboard = sprite.id.find("marker") != std::string::npos ||
            sprite.id.find("portal") != std::string::npos ||
            sprite.id.find("lid") != std::string::npos;
        const std::string renderMode = source.value(
            "render_mode", sprite.solid && !prefersBillboard
                ? std::string("voxel")
                : std::string("billboard"));
        sprite.renderMode = renderMode == "voxel" || renderMode == "voxels" || renderMode == "3d"
            ? ObjectRenderMode::Voxel
            : ObjectRenderMode::Billboard;
        sprite.yaw = source.value("yaw_degrees", 0.0) * kPi / 180.0;
        sprite.voxelDepth = std::clamp(source.value("voxel_depth", 0.0), 0.0, 3.0);

        const TextureRef* textureRef = m_textureLookup[static_cast<std::size_t>(sprite.texture)];
        std::string resolvedTexturePath;
        const auto texturePathIt = m_texturePaths.find(sprite.texture);
        if (texturePathIt != m_texturePaths.end())
            resolvedTexturePath = lowerAscii(texturePathIt->second);
        const std::string lowerId = lowerAscii(sprite.id);
        const bool fireLike = lowerId.find("torch") != std::string::npos ||
                              lowerId.find("fire") != std::string::npos ||
                              lowerId.find("ohn") != std::string::npos ||
                              resolvedTexturePath.find("torch") != std::string::npos ||
                              resolvedTexturePath.find("fire") != std::string::npos ||
                              resolvedTexturePath.find("campfire") != std::string::npos;
        const bool hasAnimationFrames = textureRef && !textureRef->animationFrames.empty();
        sprite.animated = source.value("animated", hasAnimationFrames || fireLike);
        sprite.randomAnimation = source.value("random_animation", true);
        sprite.animationMinFps = std::clamp(source.value("animation_min_fps", 7.0), 1.0, 30.0);
        sprite.animationMaxFps = std::clamp(source.value("animation_max_fps", 12.0),
                                            sprite.animationMinFps, 40.0);
        sprite.animationSeed = stableSeed(sprite.id + resolvedTexturePath);
        sprite.animationTimer = 0.015 + randomUnit(sprite.animationSeed) * 0.12;

        sprite.emitsLight = source.value("emits_light", fireLike);
        if (source.contains("light_color"))
        {
            const json& c = source["light_color"];
            sprite.lightR = jsonColor(c, 0, sprite.lightR);
            sprite.lightG = jsonColor(c, 1, sprite.lightG);
            sprite.lightB = jsonColor(c, 2, sprite.lightB);
        }
        sprite.lightRadius = std::clamp(source.value("light_radius", fireLike ? 3.4 : 0.0), 0.0, 12.0);
        sprite.lightIntensity = std::clamp(source.value("light_intensity", fireLike ? 0.95 : 0.0), 0.0, 3.0);
        sprite.lightHeight = std::clamp(source.value("light_height", fireLike ? sprite.scale * 0.68 : 0.0),
                                        -1.0, 5.0);
        sprite.lightFlicker = std::clamp(source.value("light_flicker", fireLike ? 0.20 : 0.0), 0.0, 0.75);
        sprite.lightMultiplier = 1.0;

        sprite.interactionLabel = source.value("interaction_label", std::string());
        sprite.targetInterior = source.value("target_interior", std::string());
        sprite.targetSpawn = source.value("target_spawn", std::string());
        if (existingIt != m_sprites.end())
            *existingIt = std::move(sprite);
        else
            m_sprites.push_back(std::move(sprite));
        ++added;
    }

    if (added > 0)
    {
        prepareTextureModels();
        m_sceneDirty = true;
        SDL_Log("Castle entity overlay: added %d entities for %s/%s",
                added, castleId.c_str(), mapId.c_str());
    }
    return added > 0;
}

bool BuildInteriorEngine::saveCastleEntityOverlay()
{
    if (m_loadedCastleId.empty() || m_loadedCastleMapId.empty())
    {
        m_lastError = "Objects can only be saved for a castle: map.";
        m_status = m_lastError;
        return false;
    }

    const fs::path castleRoot = ProjectRootPath() / "data" / "castles" / m_loadedCastleId;
    const fs::path materialsPath = castleRoot / "materials.json";

    // Prefer stable material IDs. Numeric texture keys are generated during
    // compilation and can change when materials are added or reordered.
    std::unordered_map<std::string, std::string> materialByTexturePath;
    try
    {
        std::ifstream materialsIn(materialsPath);
        if (materialsIn)
        {
            json materialsRoot;
            materialsIn >> materialsRoot;
            if (materialsRoot.contains("materials") && materialsRoot["materials"].is_array())
            {
                for (const json& material : materialsRoot["materials"])
                {
                    if (!material.is_object()) continue;
                    const std::string id = material.value("id", std::string());
                    const std::string texture = material.value("texture", std::string());
                    if (!id.empty() && !texture.empty())
                    {
                        const std::string normalized = lowerAscii(
                            fs::path(texture).lexically_normal().generic_string());
                        materialByTexturePath[normalized] = id;
                    }
                }
            }
        }
    }
    catch (const std::exception& ex)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Castle materials save lookup failed for %s: %s",
                    materialsPath.string().c_str(), ex.what());
    }

    json root;
    root["schema_version"] = 2;
    root["map_id"] = m_loadedCastleMapId;
    root["entities"] = json::array();

    for (const SpriteDef& sprite : m_sprites)
    {
        json entity = {
            {"id", sprite.id},
            {"x", sprite.x}, {"y", sprite.y},
            {"z_offset", sprite.zOffset},
            {"scale", sprite.scale},
            {"solid", sprite.solid},
            {"render_mode", sprite.renderMode == ObjectRenderMode::Voxel ? "voxel" : "billboard"},
            {"yaw_degrees", sprite.yaw * 180.0 / kPi},
            {"voxel_depth", sprite.voxelDepth},
            {"animated", sprite.animated},
            {"random_animation", sprite.randomAnimation},
            {"animation_min_fps", sprite.animationMinFps},
            {"animation_max_fps", sprite.animationMaxFps},
            {"emits_light", sprite.emitsLight},
            {"light_color", {sprite.lightR, sprite.lightG, sprite.lightB}},
            {"light_radius", sprite.lightRadius},
            {"light_intensity", sprite.lightIntensity},
            {"light_height", sprite.lightHeight},
            {"light_flicker", sprite.lightFlicker},
            {"interaction_label", sprite.interactionLabel},
            {"target_interior", sprite.targetInterior},
            {"target_spawn", sprite.targetSpawn}
        };

        const auto textureIt = m_texturePaths.find(sprite.texture);
        if (textureIt != m_texturePaths.end() && !textureIt->second.empty())
        {
            const std::string normalized = lowerAscii(
                fs::path(textureIt->second).lexically_normal().generic_string());
            const auto materialIt = materialByTexturePath.find(normalized);
            if (materialIt != materialByTexturePath.end())
                entity["material"] = materialIt->second;
            else
                entity["texture"] = textureIt->second;
        }
        else
        {
            entity["texture_key"] = sprite.texture;
        }

        root["entities"].push_back(std::move(entity));
    }

    const fs::path outputPath = castleRoot / "entities" /
        (m_loadedCastleMapId + ".entities.json");
    std::error_code ec;
    fs::create_directories(outputPath.parent_path(), ec);
    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        m_lastError = "Cannot save castle objects: " + outputPath.string();
        m_status = m_lastError;
        return false;
    }

    out << root.dump(2) << '\n';
    m_lastError.clear();
    m_status = U8("Objekty byly uloženy do entities JSON: ") + outputPath.string();
    return true;
}

bool BuildInteriorEngine::loadCastleGeometryOverlay(const std::string& castleId,
                                                     const std::string& mapId)
{
    const fs::path castleRoot = ProjectRootPath() / "data" / "castles" / castleId;
    const fs::path geometryPath = castleRoot / "geometry" / (mapId + ".geometry.json");
    std::error_code existsError;
    if (!fs::exists(geometryPath, existsError) || existsError)
        return false;

    json geometryRoot;
    try
    {
        std::ifstream geometryIn(geometryPath);
        if (!geometryIn) return false;
        geometryIn >> geometryRoot;
    }
    catch (const std::exception& ex)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Castle geometry overlay parse failed for %s: %s",
                    geometryPath.string().c_str(), ex.what());
        return false;
    }

    const bool editorOnly = geometryRoot.value("editor_only", false);
    if (editorOnly && !m_editorMode)
        return false;

    const bool hasWalls = geometryRoot.contains("wall_segments") && geometryRoot["wall_segments"].is_array();
    const bool hasPolygons = geometryRoot.contains("polygon_sectors") && geometryRoot["polygon_sectors"].is_array();
    if (!hasWalls && !hasPolygons) return false;

    std::unordered_map<std::string, std::string> materialTextures;
    const fs::path materialsPath = castleRoot / "materials.json";
    try
    {
        std::ifstream materialsIn(materialsPath);
        if (materialsIn)
        {
            json materialsRoot;
            materialsIn >> materialsRoot;
            if (materialsRoot.contains("materials") && materialsRoot["materials"].is_array())
            {
                for (const json& material : materialsRoot["materials"])
                {
                    if (!material.is_object()) continue;
                    const std::string id = material.value("id", std::string());
                    const std::string texture = material.value("texture", std::string());
                    if (!id.empty() && !texture.empty())
                        materialTextures[id] = texture;
                }
            }
        }
    }
    catch (...) {}

    auto normalizedPath = [](const std::string& value) {
        return lowerAscii(fs::path(value).lexically_normal().generic_string());
    };
    auto findTextureKey = [&](const std::string& relativePath) -> TextureKey
    {
        if (relativePath.empty()) return static_cast<TextureKey>('1');
        const std::string wanted = normalizedPath(relativePath);
        for (const auto& pair : m_texturePaths)
            if (normalizedPath(pair.second) == wanted) return pair.first;
        TextureKey key = 256;
        for (const auto& pair : m_textures)
            if (pair.first >= key && pair.first < 65535) key = static_cast<TextureKey>(pair.first + 1);
        while (key != 0 && m_textures.find(key) != m_textures.end()) ++key;
        if (key != 0 && loadTextureForCell(key, relativePath)) return key;
        return static_cast<TextureKey>('1');
    };

    int added = 0;
    if (hasWalls) for (const json& source : geometryRoot["wall_segments"])
    {
        if (!source.is_object()) continue;
        const std::string id = source.value("id", std::string("wall_segment_") + std::to_string(m_wallSegments.size()));
        const bool duplicate = std::any_of(m_wallSegments.begin(), m_wallSegments.end(),
            [&](const WallSegmentDef& wall) { return wall.id == id; });
        if (duplicate) continue;

        WallSegmentDef wall;
        wall.id = id;
        wall.x0 = source.value("x0", 0.0);
        wall.y0 = source.value("y0", 0.0);
        wall.x1 = source.value("x1", 1.0);
        wall.y1 = source.value("y1", 0.0);
        wall.bottomZ = source.value("bottom_z", 0.0);
        wall.topZ = source.value("top_z", 3.0);
        wall.bottomZEnd = source.value("bottom_z_end", wall.bottomZ);
        wall.topZEnd = source.value("top_z_end", wall.topZ);
        if (source.contains("top_profile") &&
            source["top_profile"].is_array())
        {
            for (const auto& height : source["top_profile"])
                if (height.is_number())
                    wall.topProfile.push_back(height.get<double>());
        }
        wall.ambient = std::clamp(source.value("ambient", 1.0), 0.15, 1.35);
        wall.textureScale = std::clamp(source.value("texture_scale", 1.0), 0.05, 8.0);
        wall.textureUOffset = source.value("texture_u_offset", 0.0);
        wall.worldAlignedTexture =
            source.value("world_aligned_texture", false);
        wall.solid = source.value("solid", true);
        wall.twoSided = source.value("two_sided", true);
        const std::string materialId = source.value("material", std::string());
        const auto materialIt = materialTextures.find(materialId);
        wall.texture = materialIt != materialTextures.end() ? findTextureKey(materialIt->second)
                                                            : textureKeyFromJson(source, "texture_key", "texture", static_cast<TextureKey>('1'));
        const bool variableHeight =
            source.contains("bottom_z_end") ||
            source.contains("top_z_end") ||
            !wall.topProfile.empty();
        if (variableHeight)
        {
            wall.topZ = std::max(wall.topZ, wall.bottomZ);
            wall.topZEnd =
                std::max(wall.topZEnd, wall.bottomZEnd);
        }
        else
        {
            if (wall.topZ < wall.bottomZ + 0.10)
                wall.topZ = wall.bottomZ + 0.10;
            wall.topZEnd = wall.topZ;
            wall.bottomZEnd = wall.bottomZ;
        }
        if (std::hypot(wall.x1 - wall.x0, wall.y1 - wall.y0) <= 0.05) continue;
        m_wallSegments.push_back(wall);
        ++added;
    }
    int addedPolygons = 0;
    if (hasPolygons)
    {
        for (const json& source : geometryRoot["polygon_sectors"])
        {
            if (!source.is_object() || !source.contains("vertices") || !source["vertices"].is_array()) continue;
            const std::string id = source.value("id", std::string("polygon_sector_") + std::to_string(m_polygonSectors.size()));
            const bool duplicate = std::any_of(m_polygonSectors.begin(), m_polygonSectors.end(),
                [&](const PolygonSectorRegion& region) { return region.id == id; });
            if (duplicate) continue;
            PolygonSectorRegion region;
            region.id = id;
            const std::string sectorId = source.value("sector_id", std::string());
            if (!sectorId.empty())
            {
                region.sector = 'A';
                for (const auto& pair : m_sectors)
                {
                    if (pair.second.id == sectorId)
                    {
                        region.sector = pair.first;
                        break;
                    }
                }
            }
            else
            {
                region.sector = sectorSymbolFromJson(source, "sector", "sector_code", 'A');
            }
            region.boundarySolid = source.value("boundary_solid", true);
            region.cutsUnderlyingFloor =
                source.value("cuts_underlying_floor", false);
            region.hasSupportBottom =
                source.value("has_support_bottom", false);
            region.supportBottomZ =
                source.value("support_bottom_z", 0.0);
            region.wallAmbient = std::clamp(source.value("wall_ambient", 1.0), 0.15, 1.35);
            region.wallTextureScale = std::clamp(source.value("wall_texture_scale", 1.0), 0.05, 8.0);
            const std::string wallMaterialId = source.value("wall_material", std::string());
            const auto wallMaterialIt = materialTextures.find(wallMaterialId);
            if (wallMaterialIt != materialTextures.end())
                region.wallTexture = findTextureKey(wallMaterialIt->second);
            else
                region.wallTexture = textureKeyFromJson(source, "wall_texture_key", "wall_texture", kNoTexture);
            for (const json& vertex : source["vertices"])
            {
                if (!vertex.is_array() || vertex.size() < 2) continue;
                region.vertices.push_back({vertex[0].get<double>(), vertex[1].get<double>()});
            }
            if (source.contains("edge_openings") && source["edge_openings"].is_array())
            {
                for (const json& openingSource : source["edge_openings"])
                {
                    if (!openingSource.is_object()) continue;
                    PolygonEdgeOpening opening;
                    opening.edge = openingSource.value("edge", 0);
                    opening.start = std::clamp(openingSource.value("start", 0.35), 0.0, 1.0);
                    opening.end = std::clamp(openingSource.value("end", 0.65), 0.0, 1.0);
                    if (opening.end < opening.start) std::swap(opening.start, opening.end);
                    opening.bottom = std::max(0.0, openingSource.value("bottom", 0.0));
                    opening.height = std::max(0.10, openingSource.value("height", 2.10));
                    region.openings.push_back(opening);
                }
            }
            if (region.vertices.size() < 3) continue;
            updatePolygonBounds(region);
            if (region.cutsUnderlyingFloor)
                m_floorCutoutSectors[
                    static_cast<unsigned char>(region.sector)] = true;
            m_polygonSectors.push_back(std::move(region));
            ++addedPolygons;
        }
    }
    if (added > 0 || addedPolygons > 0)
    {
        rebuildPolygonBoundaryWalls();
        rebuildVectorSectorLookup();
        m_sceneDirty = true;
        SDL_Log("Castle geometry overlay: added %d walls and %d polygon sectors for %s/%s",
                added, addedPolygons, castleId.c_str(), mapId.c_str());
    }
    return added > 0 || addedPolygons > 0;
}

bool BuildInteriorEngine::saveCastleGeometryOverlay()
{
    if (m_loadedCastleId.empty() || m_loadedCastleMapId.empty())
    {
        m_lastError = "Polygon geometry can only be saved for a castle: map.";
        return false;
    }

    json root;
    root["schema_version"] = 2;
    root["map_id"] = m_loadedCastleMapId;
    root["geometry_mode"] = "polygon_sectors";
    root["source_note"] = "Polygon sectors are primary geometry; the tile grid is optional compatibility data.";

    json authoredWalls = json::array();
    for (const WallSegmentDef& wall : m_wallSegments)
    {
        authoredWalls.push_back({
            {"id", wall.id},
            {"x0", wall.x0}, {"y0", wall.y0},
            {"x1", wall.x1}, {"y1", wall.y1},
            {"bottom_z", wall.bottomZ}, {"top_z", wall.topZ},
            {"bottom_z_end", wall.bottomZEnd},
            {"top_z_end", wall.topZEnd},
            {"texture_key", wall.texture},
            {"ambient", wall.ambient},
            {"texture_scale", wall.textureScale},
            {"texture_u_offset", wall.textureUOffset},
            {"world_aligned_texture", wall.worldAlignedTexture},
            {"solid", wall.solid},
            {"two_sided", wall.twoSided}
        });
        if (!wall.topProfile.empty())
            authoredWalls.back()["top_profile"] = wall.topProfile;
    }
    root["wall_segments"] = std::move(authoredWalls);

    json polygons = json::array();
    for (const PolygonSectorRegion& region : m_polygonSectors)
    {
        json vertices = json::array();
        for (const auto& vertex : region.vertices)
            vertices.push_back({vertex[0], vertex[1]});
        json openings = json::array();
        for (const PolygonEdgeOpening& opening : region.openings)
        {
            openings.push_back({
                {"edge", opening.edge},
                {"start", opening.start},
                {"end", opening.end},
                {"bottom", opening.bottom},
                {"height", opening.height}
            });
        }
        std::string sectorId;
        if (const SectorDef* sector = m_sectorLookup[static_cast<unsigned char>(region.sector)])
            sectorId = sector->id;
        polygons.push_back({
            {"id", region.id},
            {"sector_id", sectorId},
            {"sector", sectorKeyString(region.sector)},
            {"sector_code", sectorCode(region.sector)},
            {"boundary_solid", region.boundarySolid},
            {"cuts_underlying_floor", region.cutsUnderlyingFloor},
            {"has_support_bottom", region.hasSupportBottom},
            {"support_bottom_z", region.supportBottomZ},
            {"wall_texture_key", region.wallTexture},
            {"wall_ambient", region.wallAmbient},
            {"wall_texture_scale", region.wallTextureScale},
            {"edge_openings", std::move(openings)},
            {"vertices", std::move(vertices)}
        });
    }
    root["polygon_sectors"] = std::move(polygons);

    const fs::path outputPath = ProjectRootPath() / "data" / "castles" /
        m_loadedCastleId / "geometry" / (m_loadedCastleMapId + ".geometry.json");
    std::error_code ec;
    fs::create_directories(outputPath.parent_path(), ec);
    std::ofstream out(outputPath);
    if (!out)
    {
        m_lastError = "Cannot save polygon geometry: " + outputPath.string();
        return false;
    }
    out << root.dump(2) << '\n';
    m_lastError.clear();
    m_status = U8("Polygonální geometrie byla uložena do geometry JSON.");
    return true;
}

void BuildInteriorEngine::clearTextures()
{
    for (auto& pair : m_textures)
    {
        if (pair.second.texture)
            SDL_DestroyTexture(pair.second.texture);
    }
    m_textures.clear();
    m_textureLookup.fill(nullptr);
}

void BuildInteriorEngine::rebuildTextureLookup()
{
    m_textureLookup.fill(nullptr);
    for (const auto& pair : m_textures)
        m_textureLookup[static_cast<std::size_t>(pair.first)] = &pair.second;
}

void BuildInteriorEngine::rebuildSectorLookup()
{
    m_sectorLookup.fill(nullptr);
    for (const auto& pair : m_sectors)
        m_sectorLookup[static_cast<unsigned char>(pair.first)] = &pair.second;
}

void BuildInteriorEngine::buildVoxelModel(TextureRef& texture)
{
    texture.voxelCells.clear();
    texture.voxelGridW = 0;
    texture.voxelGridH = 0;
    if (texture.w <= 0 || texture.h <= 0 || texture.pixels.empty())
        return;

    const int longest = std::max(texture.w, texture.h);
    const int gridW = std::max(2, static_cast<int>(std::lround(
        static_cast<double>(kVoxelMaxGrid) * texture.w / longest)));
    const int gridH = std::max(2, static_cast<int>(std::lround(
        static_cast<double>(kVoxelMaxGrid) * texture.h / longest)));
    texture.voxelGridW = gridW;
    texture.voxelGridH = gridH;

    std::vector<std::uint8_t> occupied(static_cast<std::size_t>(gridW) * gridH, 0u);
    std::vector<std::uint32_t> colors(static_cast<std::size_t>(gridW) * gridH, 0u);

    for (int gy = 0; gy < gridH; ++gy)
    {
        const int y0 = gy * texture.h / gridH;
        const int y1 = std::max(y0 + 1, (gy + 1) * texture.h / gridH);
        for (int gx = 0; gx < gridW; ++gx)
        {
            const int x0 = gx * texture.w / gridW;
            const int x1 = std::max(x0 + 1, (gx + 1) * texture.w / gridW);
            std::uint64_t sumA = 0, sumR = 0, sumG = 0, sumB = 0;
            int visibleSamples = 0;
            int samples = 0;
            for (int py = y0; py < std::min(y1, texture.h); ++py)
            {
                for (int px = x0; px < std::min(x1, texture.w); ++px)
                {
                    const std::uint32_t color = texture.pixels[static_cast<std::size_t>(py) * texture.w + px];
                    const int a = static_cast<int>((color >> 24u) & 0xffu);
                    ++samples;
                    if (a < 24) continue;
                    ++visibleSamples;
                    sumA += static_cast<std::uint64_t>(a);
                    sumR += static_cast<std::uint64_t>((color >> 16u) & 0xffu) * a;
                    sumG += static_cast<std::uint64_t>((color >> 8u) & 0xffu) * a;
                    sumB += static_cast<std::uint64_t>(color & 0xffu) * a;
                }
            }

            if (visibleSamples == 0 || visibleSamples * 5 < std::max(1, samples))
                continue;

            const int r = sumA > 0 ? static_cast<int>(sumR / sumA) : 255;
            const int g = sumA > 0 ? static_cast<int>(sumG / sumA) : 255;
            const int b = sumA > 0 ? static_cast<int>(sumB / sumA) : 255;
            const std::size_t index = static_cast<std::size_t>(gy) * gridW + gx;
            occupied[index] = 1u;
            colors[index] = argb(r, g, b, 255);
        }
    }

    auto isOccupied = [&](int x, int y) {
        return x >= 0 && x < gridW && y >= 0 && y < gridH &&
            occupied[static_cast<std::size_t>(y) * gridW + x] != 0u;
    };

    texture.voxelCells.reserve(static_cast<std::size_t>(gridW) * gridH);
    for (int gy = 0; gy < gridH; ++gy)
    {
        for (int gx = 0; gx < gridW; ++gx)
        {
            const std::size_t index = static_cast<std::size_t>(gy) * gridW + gx;
            if (!occupied[index]) continue;
            VoxelCell cell;
            cell.x0 = static_cast<float>(gx) / gridW - 0.5f;
            cell.x1 = static_cast<float>(gx + 1) / gridW - 0.5f;
            cell.z0 = 1.0f - static_cast<float>(gy + 1) / gridH;
            cell.z1 = 1.0f - static_cast<float>(gy) / gridH;
            cell.color = colors[index];
            if (!isOccupied(gx - 1, gy)) cell.exposed |= kVoxelLeft;
            if (!isOccupied(gx + 1, gy)) cell.exposed |= kVoxelRight;
            if (!isOccupied(gx, gy - 1)) cell.exposed |= kVoxelTop;
            if (!isOccupied(gx, gy + 1)) cell.exposed |= kVoxelBottom;
            texture.voxelCells.push_back(cell);
        }
    }
}

void BuildInteriorEngine::buildVoxelFrame(TextureFrame& texture)
{
    texture.voxelCells.clear();
    texture.voxelGridW = 0;
    texture.voxelGridH = 0;
    if (texture.w <= 0 || texture.h <= 0 || texture.pixels.empty())
        return;

    const int longest = std::max(texture.w, texture.h);
    const int gridW = std::max(2, static_cast<int>(std::lround(
        static_cast<double>(kVoxelMaxGrid) * texture.w / longest)));
    const int gridH = std::max(2, static_cast<int>(std::lround(
        static_cast<double>(kVoxelMaxGrid) * texture.h / longest)));
    texture.voxelGridW = gridW;
    texture.voxelGridH = gridH;

    std::vector<std::uint8_t> occupied(static_cast<std::size_t>(gridW) * gridH, 0u);
    std::vector<std::uint32_t> colors(static_cast<std::size_t>(gridW) * gridH, 0u);

    for (int gy = 0; gy < gridH; ++gy)
    {
        const int y0 = gy * texture.h / gridH;
        const int y1 = std::max(y0 + 1, (gy + 1) * texture.h / gridH);
        for (int gx = 0; gx < gridW; ++gx)
        {
            const int x0 = gx * texture.w / gridW;
            const int x1 = std::max(x0 + 1, (gx + 1) * texture.w / gridW);
            std::uint64_t sumA = 0, sumR = 0, sumG = 0, sumB = 0;
            int visibleSamples = 0;
            int samples = 0;
            for (int py = y0; py < std::min(y1, texture.h); ++py)
            {
                for (int px = x0; px < std::min(x1, texture.w); ++px)
                {
                    const std::uint32_t color = texture.pixels[static_cast<std::size_t>(py) * texture.w + px];
                    const int a = static_cast<int>((color >> 24u) & 0xffu);
                    ++samples;
                    if (a < 24) continue;
                    ++visibleSamples;
                    sumA += static_cast<std::uint64_t>(a);
                    sumR += static_cast<std::uint64_t>((color >> 16u) & 0xffu) * a;
                    sumG += static_cast<std::uint64_t>((color >> 8u) & 0xffu) * a;
                    sumB += static_cast<std::uint64_t>(color & 0xffu) * a;
                }
            }

            if (visibleSamples == 0 || visibleSamples * 5 < std::max(1, samples))
                continue;

            const int r = sumA > 0 ? static_cast<int>(sumR / sumA) : 255;
            const int g = sumA > 0 ? static_cast<int>(sumG / sumA) : 255;
            const int b = sumA > 0 ? static_cast<int>(sumB / sumA) : 255;
            const std::size_t index = static_cast<std::size_t>(gy) * gridW + gx;
            occupied[index] = 1u;
            colors[index] = argb(r, g, b, 255);
        }
    }

    auto isOccupied = [&](int x, int y) {
        return x >= 0 && x < gridW && y >= 0 && y < gridH &&
            occupied[static_cast<std::size_t>(y) * gridW + x] != 0u;
    };

    texture.voxelCells.reserve(static_cast<std::size_t>(gridW) * gridH);
    for (int gy = 0; gy < gridH; ++gy)
    {
        for (int gx = 0; gx < gridW; ++gx)
        {
            const std::size_t index = static_cast<std::size_t>(gy) * gridW + gx;
            if (!occupied[index]) continue;
            VoxelCell cell;
            cell.x0 = static_cast<float>(gx) / gridW - 0.5f;
            cell.x1 = static_cast<float>(gx + 1) / gridW - 0.5f;
            cell.z0 = 1.0f - static_cast<float>(gy + 1) / gridH;
            cell.z1 = 1.0f - static_cast<float>(gy) / gridH;
            cell.color = colors[index];
            if (!isOccupied(gx - 1, gy)) cell.exposed |= kVoxelLeft;
            if (!isOccupied(gx + 1, gy)) cell.exposed |= kVoxelRight;
            if (!isOccupied(gx, gy - 1)) cell.exposed |= kVoxelTop;
            if (!isOccupied(gx, gy + 1)) cell.exposed |= kVoxelBottom;
            texture.voxelCells.push_back(cell);
        }
    }
}


bool BuildInteriorEngine::loadTextureFrame(const std::string& absolutePath, TextureFrame& frame)
{
    SDL_Surface* loaded = IMG_Load(absolutePath.c_str());
    if (!loaded)
        return false;

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(loaded);
    if (!converted)
        return false;

    frame = TextureFrame{};
    frame.w = converted->w;
    frame.h = converted->h;
    frame.pixels.resize(static_cast<std::size_t>(frame.w) * static_cast<std::size_t>(frame.h));
    for (int y = 0; y < frame.h; ++y)
    {
        const auto* src = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(converted->pixels) + y * converted->pitch);
        std::copy(src, src + frame.w, frame.pixels.begin() + static_cast<std::size_t>(y) * frame.w);
    }
    SDL_FreeSurface(converted);
    return true;
}

void BuildInteriorEngine::loadTextureAnimationFrames(TextureRef& texture)
{
    if (!texture.animationFrames.empty() || texture.path.empty())
        return;

    const fs::path basePath(resolveProjectPath(texture.path));
    const std::string stem = basePath.stem().string();
    const std::string extension = basePath.extension().string();
    for (int frameIndex = 0; frameIndex < 32; ++frameIndex)
    {
        char suffix[24]{};
        std::snprintf(suffix, sizeof(suffix), "_anim_%02d", frameIndex);
        const fs::path framePath =
            basePath.parent_path() / (stem + suffix + extension);
        std::error_code existsError;
        if (!fs::exists(framePath, existsError) || existsError)
            break;

        TextureFrame frame;
        if (!loadTextureFrame(framePath.string(), frame))
            break;
        texture.animationFrames.push_back(std::move(frame));
    }
}

void BuildInteriorEngine::prepareTextureModels()
{
    std::array<bool, 65536> needsAnimation{};
    std::array<bool, 65536> needsVoxels{};
    for (const SpriteDef& sprite : m_sprites)
    {
        const std::size_t key = static_cast<std::size_t>(sprite.texture);
        needsAnimation[key] = needsAnimation[key] || sprite.animated;
        needsVoxels[key] =
            needsVoxels[key] || sprite.renderMode == ObjectRenderMode::Voxel;
    }

    for (auto& pair : m_textures)
    {
        const std::size_t key = static_cast<std::size_t>(pair.first);
        TextureRef& texture = pair.second;
        if (needsAnimation[key])
            loadTextureAnimationFrames(texture);
        if (!needsVoxels[key])
            continue;
        if (texture.voxelCells.empty())
            buildVoxelModel(texture);
        for (TextureFrame& frame : texture.animationFrames)
        {
            if (frame.voxelCells.empty())
                buildVoxelFrame(frame);
        }
    }
    rebuildTextureLookup();
}

const BuildInteriorEngine::TextureFrame* BuildInteriorEngine::activeAnimationFrame(
    const TextureRef& texture, const SpriteDef& sprite) const
{
    if (!sprite.animated || texture.animationFrames.empty())
        return nullptr;
    const int count = static_cast<int>(texture.animationFrames.size());
    const int index = count > 0 ? ((sprite.animationFrame % count) + count) % count : 0;
    return &texture.animationFrames[static_cast<std::size_t>(index)];
}

bool BuildInteriorEngine::loadTextureForCell(TextureKey key, const std::string& relativePath)
{
    if (!m_renderer)
        return false;

    const std::string path = resolveProjectPath(relativePath);
    SDL_Surface* loaded = IMG_Load(path.c_str());
    if (!loaded)
    {
        m_status = "Missing interior texture: " + path;
        return false;
    }

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(loaded);
    if (!converted)
    {
        m_status = "Failed to convert interior texture: " + path;
        return false;
    }

    TextureRef ref;
    ref.w = converted->w;
    ref.h = converted->h;
    ref.path = relativePath;
    ref.pixels.resize(static_cast<std::size_t>(ref.w) * static_cast<std::size_t>(ref.h));
    for (int y = 0; y < ref.h; ++y)
    {
        const auto* src = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(converted->pixels) + y * converted->pitch);
        std::copy(src, src + ref.w, ref.pixels.begin() + static_cast<std::size_t>(y) * ref.w);
    }
    ref.texture = SDL_CreateTextureFromSurface(m_renderer, converted);
    SDL_FreeSurface(converted);

    if (ref.texture)
    {
        SDL_SetTextureBlendMode(ref.texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(ref.texture, SDL_ScaleModeNearest);
#endif
    }

    auto old = m_textures.find(key);
    if (old != m_textures.end() && old->second.texture)
        SDL_DestroyTexture(old->second.texture);
    m_textures[key] = std::move(ref);
    m_texturePaths[key] = relativePath;
    rebuildTextureLookup();
    m_sceneDirty = true;
    return true;
}

void BuildInteriorEngine::ensureSceneBuffer(int width, int height)
{
    if (width == m_sceneW && height == m_sceneH && m_sceneTexture)
        return;

    if (m_sceneTexture)
        SDL_DestroyTexture(m_sceneTexture);

    m_sceneW = width;
    m_sceneH = height;
    m_framebuffer.assign(static_cast<std::size_t>(width) * height, argb(0, 0, 0));
    m_zBuffer.assign(width, kHuge);
    m_dynamicDepthBuffer.assign(static_cast<std::size_t>(width) * height, kHuge);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // V15: nearest-neighbour upscale to reduce seam shimmer and white filtering lines.
    m_sceneTexture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, width, height);
    if (m_sceneTexture)
    {
        SDL_SetTextureBlendMode(m_sceneTexture, SDL_BLENDMODE_NONE);
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(m_sceneTexture, SDL_ScaleModeNearest);
#endif
    }
}

bool BuildInteriorEngine::isInside(int x, int y) const
{
    return y >= 0 && y < static_cast<int>(m_grid.size()) &&
           x >= 0 && x < static_cast<int>(m_grid[y].size());
}

BuildInteriorEngine::TextureKey BuildInteriorEngine::cellAt(int x, int y) const
{
    return isInside(x, y) ? m_grid[y][x] : static_cast<TextureKey>('#');
}

char BuildInteriorEngine::sectorSymbolAt(int x, int y) const
{
    if (y < 0 || y >= static_cast<int>(m_sectorGrid.size()) ||
        x < 0 || x >= static_cast<int>(m_sectorGrid[y].size()))
        return 'A';
    const char value = m_sectorGrid[y][x];
    return value == '\0' ? 'A' : value;
}

const BuildInteriorEngine::SectorDef& BuildInteriorEngine::sectorAt(int x, int y) const
{
    const char symbol = sectorSymbolAt(x, y);
    const SectorDef* sector = m_sectorLookup[static_cast<unsigned char>(symbol)];
    if (sector)
        return *sector;
    static SectorDef fallback;
    return fallback;
}

bool BuildInteriorEngine::hasVectorGeometry() const
{
    return !m_polygonSectors.empty();
}

bool BuildInteriorEngine::pointInPolygon(const PolygonSectorRegion& region,
                                         double x, double y)
{
    if (region.vertices.size() < 3 ||
        x < region.minX || x > region.maxX ||
        y < region.minY || y > region.maxY)
        return false;

    bool inside = false;
    const std::size_t count = region.vertices.size();
    for (std::size_t i = 0, j = count - 1; i < count; j = i++)
    {
        const double xi = region.vertices[i][0];
        const double yi = region.vertices[i][1];
        const double xj = region.vertices[j][0];
        const double yj = region.vertices[j][1];
        const bool crosses = ((yi > y) != (yj > y)) &&
            (x < (xj - xi) * (y - yi) / ((yj - yi) + 1.0e-20) + xi);
        if (crosses) inside = !inside;
    }
    return inside;
}

double BuildInteriorEngine::polygonSignedArea(const PolygonSectorRegion& region)
{
    if (region.vertices.size() < 3) return 0.0;
    double area = 0.0;
    for (std::size_t i = 0; i < region.vertices.size(); ++i)
    {
        const auto& a = region.vertices[i];
        const auto& b = region.vertices[(i + 1) % region.vertices.size()];
        area += a[0] * b[1] - b[0] * a[1];
    }
    return area * 0.5;
}

bool BuildInteriorEngine::polygonIsSimple(const PolygonSectorRegion& region)
{
    const std::size_t count = region.vertices.size();
    if (count < 3 || std::abs(polygonSignedArea(region)) < 1.0e-5)
        return false;

    auto orientation = [](const std::array<double, 2>& a,
                          const std::array<double, 2>& b,
                          const std::array<double, 2>& c) {
        return (b[0] - a[0]) * (c[1] - a[1]) -
               (b[1] - a[1]) * (c[0] - a[0]);
    };
    auto intersects = [&](const std::array<double, 2>& a,
                          const std::array<double, 2>& b,
                          const std::array<double, 2>& c,
                          const std::array<double, 2>& d) {
        const double abC = orientation(a, b, c);
        const double abD = orientation(a, b, d);
        const double cdA = orientation(c, d, a);
        const double cdB = orientation(c, d, b);
        constexpr double eps = 1.0e-8;
        return ((abC > eps && abD < -eps) || (abC < -eps && abD > eps)) &&
               ((cdA > eps && cdB < -eps) || (cdA < -eps && cdB > eps));
    };

    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t iNext = (i + 1) % count;
        for (std::size_t j = i + 1; j < count; ++j)
        {
            const std::size_t jNext = (j + 1) % count;
            if (i == j || iNext == j || jNext == i) continue;
            if (i == 0 && jNext == 0) continue;
            if (intersects(region.vertices[i], region.vertices[iNext],
                           region.vertices[j], region.vertices[jNext]))
                return false;
        }
    }
    return true;
}

std::vector<std::array<int, 3>> BuildInteriorEngine::triangulatePolygon(
    const PolygonSectorRegion& region)
{
    std::vector<std::array<int, 3>> triangles;
    const int count = static_cast<int>(region.vertices.size());
    if (count < 3)
        return triangles;
    if (count == 3)
    {
        triangles.push_back({0, 1, 2});
        return triangles;
    }

    std::vector<int> remaining(static_cast<std::size_t>(count));
    std::iota(remaining.begin(), remaining.end(), 0);
    const bool counterClockwise = polygonSignedArea(region) > 0.0;

    auto cross = [&](int ia, int ib, int ic) {
        const auto& a = region.vertices[static_cast<std::size_t>(ia)];
        const auto& b = region.vertices[static_cast<std::size_t>(ib)];
        const auto& c = region.vertices[static_cast<std::size_t>(ic)];
        return (b[0] - a[0]) * (c[1] - a[1]) -
               (b[1] - a[1]) * (c[0] - a[0]);
    };
    auto pointInTriangle = [&](const std::array<double, 2>& point,
                               int ia, int ib, int ic) {
        const auto& a = region.vertices[static_cast<std::size_t>(ia)];
        const auto& b = region.vertices[static_cast<std::size_t>(ib)];
        const auto& c = region.vertices[static_cast<std::size_t>(ic)];
        auto side = [](const std::array<double, 2>& p,
                       const std::array<double, 2>& q,
                       const std::array<double, 2>& r) {
            return (q[0] - p[0]) * (r[1] - p[1]) -
                   (q[1] - p[1]) * (r[0] - p[0]);
        };
        const double ab = side(a, b, point);
        const double bc = side(b, c, point);
        const double ca = side(c, a, point);
        constexpr double epsilon = 1.0e-9;
        const bool hasNegative = ab < -epsilon || bc < -epsilon || ca < -epsilon;
        const bool hasPositive = ab > epsilon || bc > epsilon || ca > epsilon;
        return !(hasNegative && hasPositive);
    };

    int guard = count * count;
    while (remaining.size() > 3 && guard > 0)
    {
        --guard;
        bool removedEar = false;
        for (std::size_t i = 0; i < remaining.size(); ++i)
        {
            const int previous =
                remaining[(i + remaining.size() - 1) % remaining.size()];
            const int current = remaining[i];
            const int next = remaining[(i + 1) % remaining.size()];
            const double corner = cross(previous, current, next);
            if ((counterClockwise && corner <= 1.0e-9) ||
                (!counterClockwise && corner >= -1.0e-9))
                continue;

            bool containsVertex = false;
            for (const int candidate : remaining)
            {
                if (candidate == previous || candidate == current ||
                    candidate == next)
                    continue;
                if (pointInTriangle(
                        region.vertices[static_cast<std::size_t>(candidate)],
                        previous, current, next))
                {
                    containsVertex = true;
                    break;
                }
            }
            if (containsVertex)
                continue;

            triangles.push_back({previous, current, next});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            removedEar = true;
            break;
        }
        if (!removedEar)
            break;
    }

    if (remaining.size() == 3)
        triangles.push_back({remaining[0], remaining[1], remaining[2]});

    // All current castle polygons are simple, but retaining a deterministic
    // fallback keeps editor-authored convex shapes visible while the validator
    // reports a malformed concave outline.
    if (triangles.size() != static_cast<std::size_t>(count - 2))
    {
        triangles.clear();
        for (int i = 1; i + 1 < count; ++i)
            triangles.push_back({0, i, i + 1});
    }
    return triangles;
}

void BuildInteriorEngine::updatePolygonBounds(PolygonSectorRegion& region)
{
    if (region.vertices.empty())
    {
        region.minX = region.maxX = 0.0;
        region.minY = region.maxY = 0.0;
        region.outlineSimple = false;
        region.triangles.clear();
        return;
    }
    region.minX = region.maxX = region.vertices.front()[0];
    region.minY = region.maxY = region.vertices.front()[1];
    for (const auto& vertex : region.vertices)
    {
        region.minX = std::min(region.minX, vertex[0]);
        region.minY = std::min(region.minY, vertex[1]);
        region.maxX = std::max(region.maxX, vertex[0]);
        region.maxY = std::max(region.maxY, vertex[1]);
    }
    region.outlineSimple = polygonIsSimple(region);
    region.triangles = triangulatePolygon(region);
}

const BuildInteriorEngine::PolygonSectorRegion* BuildInteriorEngine::polygonRegionAtWorld(
    double worldX, double worldY) const
{
    // Polygon validity is checked when geometry is loaded or edited. Repeating
    // polygonIsSimple() for every floor, ceiling and light sample turned one
    // screen frame into millions of segment-intersection tests.
    for (auto it = m_polygonSectors.rbegin(); it != m_polygonSectors.rend(); ++it)
        if (it->outlineSimple && pointInPolygon(*it, worldX, worldY))
            return &*it;
    return nullptr;
}

void BuildInteriorEngine::rebuildVectorSectorLookup()
{
    m_vectorSectorLookup.clear();
    m_floorCutoutPolygonIndices.clear();
    m_floorCutoutMinX = 1.0e30;
    m_floorCutoutMinY = 1.0e30;
    m_floorCutoutMaxX = -1.0e30;
    m_floorCutoutMaxY = -1.0e30;
    m_vectorSectorLookupW = 0;
    m_vectorSectorLookupH = 0;
    m_vectorLookupOriginX = 0.0;
    m_vectorLookupOriginY = 0.0;
    if (m_polygonSectors.empty())
        return;

    for (std::size_t index = 0; index < m_polygonSectors.size(); ++index)
    {
        if (m_polygonSectors[index].cutsUnderlyingFloor)
        {
            m_floorCutoutPolygonIndices.push_back(index);
            m_floorCutoutMinX = std::min(
                m_floorCutoutMinX, m_polygonSectors[index].minX);
            m_floorCutoutMinY = std::min(
                m_floorCutoutMinY, m_polygonSectors[index].minY);
            m_floorCutoutMaxX = std::max(
                m_floorCutoutMaxX, m_polygonSectors[index].maxX);
            m_floorCutoutMaxY = std::max(
                m_floorCutoutMaxY, m_polygonSectors[index].maxY);
        }
    }

    double minX = m_polygonSectors.front().minX;
    double minY = m_polygonSectors.front().minY;
    double maxX = m_polygonSectors.front().maxX;
    double maxY = m_polygonSectors.front().maxY;
    for (const PolygonSectorRegion& region : m_polygonSectors)
    {
        minX = std::min(minX, region.minX);
        minY = std::min(minY, region.minY);
        maxX = std::max(maxX, region.maxX);
        maxY = std::max(maxY, region.maxY);
    }

    // A small margin makes points exactly on the outer wall deterministic and
    // also allows polygon maps to use negative coordinates.
    m_vectorLookupOriginX = std::floor(minX) - 1.0;
    m_vectorLookupOriginY = std::floor(minY) - 1.0;
    const double width = std::ceil(maxX) + 1.0 - m_vectorLookupOriginX;
    const double height = std::ceil(maxY) + 1.0 - m_vectorLookupOriginY;
    m_vectorSectorLookupW = std::max(1, static_cast<int>(std::ceil(width * kVectorSectorSubdiv)));
    m_vectorSectorLookupH = std::max(1, static_cast<int>(std::ceil(height * kVectorSectorSubdiv)));
    m_vectorSectorLookup.assign(
        static_cast<std::size_t>(m_vectorSectorLookupW) * m_vectorSectorLookupH, '\0');

    for (int sy = 0; sy < m_vectorSectorLookupH; ++sy)
    {
        const double worldY = m_vectorLookupOriginY +
            (sy + 0.5) / static_cast<double>(kVectorSectorSubdiv);
        for (int sx = 0; sx < m_vectorSectorLookupW; ++sx)
        {
            const double worldX = m_vectorLookupOriginX +
                (sx + 0.5) / static_cast<double>(kVectorSectorSubdiv);
            char symbol = '\0';
            for (auto it = m_polygonSectors.rbegin(); it != m_polygonSectors.rend(); ++it)
            {
                // Polygon outlines are validated once when loaded. Repeating
                // the O(edges²) simplicity test for every 1/8 m lookup sample
                // was the dominant pause while opening the courtyard.
                if (it->outlineSimple &&
                    pointInPolygon(*it, worldX, worldY))
                {
                    symbol = it->sector;
                    break;
                }
            }
            m_vectorSectorLookup[static_cast<std::size_t>(sy) * m_vectorSectorLookupW + sx] = symbol;
        }
    }
}

void BuildInteriorEngine::rebuildPolygonBoundaryWalls()
{
    m_polygonBoundaryWalls.clear();
    m_polygonPortals.clear();
    if (m_polygonSectors.empty())
        return;

    struct EdgeContribution
    {
        const PolygonSectorRegion* region = nullptr;
        int edge = 0;
        std::array<double, 2> a{};
        std::array<double, 2> b{};
        double sourceStart = 0.0;
        double sourceEnd = 1.0;
        bool reversed = false;
    };
    std::unordered_map<std::string, std::vector<EdgeContribution>> groups;

    auto quantizedPoint = [](const std::array<double, 2>& p) {
        const long long x = static_cast<long long>(std::llround(p[0] * 10000.0));
        const long long y = static_cast<long long>(std::llround(p[1] * 10000.0));
        return std::pair<long long, long long>(x, y);
    };
    auto edgeKey = [&](const std::array<double, 2>& a,
                       const std::array<double, 2>& b,
                       bool& reversed) {
        auto qa = quantizedPoint(a);
        auto qb = quantizedPoint(b);
        reversed = qb < qa;
        if (reversed) std::swap(qa, qb);
        return std::to_string(qa.first) + ":" + std::to_string(qa.second) + "|" +
               std::to_string(qb.first) + ":" + std::to_string(qb.second);
    };

    // Split each edge at every collinear polygon vertex. This normalizes
    // T-junctions and partially shared edges before grouping, so a long wall
    // and two shorter neighbouring room edges become the same mathematical
    // subsegments instead of overlapping and closing each other's openings.
    std::vector<std::array<double, 2>> allVertices;
    for (const PolygonSectorRegion& region : m_polygonSectors)
        allVertices.insert(allVertices.end(), region.vertices.begin(), region.vertices.end());

    for (const PolygonSectorRegion& region : m_polygonSectors)
    {
        if (!region.boundarySolid || region.vertices.size() < 3 ||
            !region.outlineSimple)
            continue;
        for (int edge = 0; edge < static_cast<int>(region.vertices.size()); ++edge)
        {
            const auto& sourceA = region.vertices[static_cast<std::size_t>(edge)];
            const auto& sourceB = region.vertices[(static_cast<std::size_t>(edge) + 1u) % region.vertices.size()];
            const double vx = sourceB[0] - sourceA[0];
            const double vy = sourceB[1] - sourceA[1];
            const double lengthSquared = vx * vx + vy * vy;
            if (lengthSquared <= 0.0001)
                continue;

            std::vector<double> cuts{0.0, 1.0};
            for (const auto& vertex : allVertices)
            {
                const double wx = vertex[0] - sourceA[0];
                const double wy = vertex[1] - sourceA[1];
                const double t = (wx * vx + wy * vy) / lengthSquared;
                if (t <= 0.0001 || t >= 0.9999) continue;
                const double nearestX = sourceA[0] + vx * t;
                const double nearestY = sourceA[1] + vy * t;
                const double dx = vertex[0] - nearestX;
                const double dy = vertex[1] - nearestY;
                if (dx * dx + dy * dy <= 1.0e-8)
                    cuts.push_back(t);
            }
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end(),
                                   [](double lhs, double rhs) {
                                       return std::abs(lhs - rhs) < 1.0e-6;
                                   }), cuts.end());

            for (std::size_t cut = 0; cut + 1 < cuts.size(); ++cut)
            {
                const double t0 = cuts[cut];
                const double t1 = cuts[cut + 1];
                if (t1 - t0 <= 1.0e-6) continue;
                std::array<double, 2> a{
                    sourceA[0] + vx * t0,
                    sourceA[1] + vy * t0
                };
                std::array<double, 2> b{
                    sourceA[0] + vx * t1,
                    sourceA[1] + vy * t1
                };
                bool reversed = false;
                groups[edgeKey(a, b, reversed)].push_back(
                    {&region, edge, a, b, t0, t1, reversed});
            }
        }
    }

    auto appendWall = [&](const std::string& id,
                          const PolygonSectorRegion& region,
                          const std::array<double, 2>& a,
                          const std::array<double, 2>& b,
                          double t0, double t1,
                          double bottomZ, double topZ,
                          bool collisionSolid = true)
    {
        if (t1 - t0 <= 0.0005 || topZ - bottomZ <= 0.02)
            return;
        WallSegmentDef wall;
        wall.id = id;
        wall.x0 = a[0] + (b[0] - a[0]) * t0;
        wall.y0 = a[1] + (b[1] - a[1]) * t0;
        wall.x1 = a[0] + (b[0] - a[0]) * t1;
        wall.y1 = a[1] + (b[1] - a[1]) * t1;
        wall.bottomZ = bottomZ;
        wall.topZ = topZ;
        wall.bottomZEnd = bottomZ;
        wall.topZEnd = topZ;
        const SectorDef* sector = m_sectorLookup[static_cast<unsigned char>(region.sector)];
        wall.texture = region.wallTexture != '\0'
            ? region.wallTexture
            : (sector ? sector->boundaryTexture : '1');
        wall.ambient = region.wallAmbient * (sector ? sector->ambient : 1.0);
        wall.textureScale = region.wallTextureScale;
        wall.solid = collisionSolid;
        wall.twoSided = true;
        m_polygonBoundaryWalls.push_back(std::move(wall));
    };

    int wallCounter = 0;
    for (const auto& groupPair : groups)
    {
        const std::vector<EdgeContribution>& contributions = groupPair.second;
        if (contributions.empty()) continue;
        const EdgeContribution& base = contributions.front();
        const PolygonSectorRegion& region = *base.region;
        const auto& a = base.a;
        const auto& b = base.b;
        const double midX = (a[0] + b[0]) * 0.5;
        const double midY = (a[1] + b[1]) * 0.5;
        double floorZ = std::numeric_limits<double>::infinity();
        double wallTop = -std::numeric_limits<double>::infinity();
        for (const EdgeContribution& contribution : contributions)
        {
            const SectorDef* ownerSector = m_sectorLookup[
                static_cast<unsigned char>(contribution.region->sector)];
            if (!ownerSector) continue;
            const double ownerFloor = floorHeightAt(*ownerSector, midX, midY);
            const double ownerCeiling = ceilingHeightAt(*ownerSector, midX, midY);
            const double ownerTop = ownerSector->wallHeight > 0.0
                ? std::min(ownerCeiling, ownerFloor + ownerSector->wallHeight)
                : ownerCeiling;
            floorZ = std::min(floorZ, ownerFloor);
            wallTop = std::max(wallTop, ownerTop);
        }

        if (!std::isfinite(floorZ)) floorZ = 0.0;
        if (!std::isfinite(wallTop) || wallTop <= floorZ + 0.05) wallTop = floorZ + 3.0;

        struct OpeningInterval
        {
            double start = 0.0;
            double end = 0.0;
            double bottomZ = 0.0;
            double topZ = 0.0;
            char sectorA = '\0';
            char sectorB = '\0';
        };
        std::vector<OpeningInterval> rawOpenings;
        for (const EdgeContribution& contribution : contributions)
        {
            const SectorDef* ownerSector = m_sectorLookup[
                static_cast<unsigned char>(contribution.region->sector)];
            const double ownerMidX = (contribution.a[0] + contribution.b[0]) * 0.5;
            const double ownerMidY = (contribution.a[1] + contribution.b[1]) * 0.5;
            const double ownerFloor = ownerSector
                ? floorHeightAt(*ownerSector, ownerMidX, ownerMidY) : floorZ;
            for (const PolygonEdgeOpening& source : contribution.region->openings)
            {
                if (source.edge != contribution.edge) continue;
                const double clippedStart = std::max(source.start, contribution.sourceStart);
                const double clippedEnd = std::min(source.end, contribution.sourceEnd);
                if (clippedEnd <= clippedStart + 1.0e-6) continue;
                const double subLength = contribution.sourceEnd - contribution.sourceStart;
                double start = (clippedStart - contribution.sourceStart) / subLength;
                double end = (clippedEnd - contribution.sourceStart) / subLength;
                // First map the interval to the group's canonical direction,
                // then to the direction used by the base segment that will be
                // rendered. This keeps openings aligned even when adjacent
                // polygons list their shared edge in opposite directions.
                if (contribution.reversed)
                {
                    const double mappedStart = 1.0 - end;
                    end = 1.0 - start;
                    start = mappedStart;
                }
                if (base.reversed)
                {
                    const double mappedStart = 1.0 - end;
                    end = 1.0 - start;
                    start = mappedStart;
                }
                rawOpenings.push_back({
                    std::clamp(start, 0.0, 1.0),
                    std::clamp(end, 0.0, 1.0),
                    ownerFloor + source.bottom,
                    ownerFloor + source.bottom + source.height,
                    contribution.region->sector,
                    '\0'
                });
            }
        }

        std::array<bool, 256> edgeSectors{};
        int distinctSectorCount = 0;
        for (const EdgeContribution& contribution : contributions)
        {
            const unsigned char key =
                static_cast<unsigned char>(contribution.region->sector);
            if (!edgeSectors[key])
            {
                edgeSectors[key] = true;
                ++distinctSectorCount;
            }
        }

        std::vector<OpeningInterval> openings;
        if (distinctSectorCount >= 2)
        {
            // A shared boundary is passable only where both neighbouring
            // sectors describe an aperture.  Intersecting the two definitions
            // removes one-sided stair walls and also gives the portal renderer
            // the real vertical opening (max floor, min ceiling).
            for (std::size_t i = 0; i < rawOpenings.size(); ++i)
            {
                for (std::size_t j = i + 1; j < rawOpenings.size(); ++j)
                {
                    if (rawOpenings[i].sectorA == rawOpenings[j].sectorA)
                        continue;
                    OpeningInterval portal;
                    portal.start = std::max(rawOpenings[i].start, rawOpenings[j].start);
                    portal.end = std::min(rawOpenings[i].end, rawOpenings[j].end);
                    portal.bottomZ = std::max(rawOpenings[i].bottomZ, rawOpenings[j].bottomZ);
                    portal.topZ = std::min(rawOpenings[i].topZ, rawOpenings[j].topZ);
                    portal.sectorA = rawOpenings[i].sectorA;
                    portal.sectorB = rawOpenings[j].sectorA;
                    if (portal.end > portal.start + 0.0005 &&
                        portal.topZ > portal.bottomZ + 0.02)
                        openings.push_back(portal);
                }
            }
        }
        else
        {
            openings = rawOpenings;
        }

        std::sort(openings.begin(), openings.end(),
                  [](const OpeningInterval& lhs, const OpeningInterval& rhs) {
                      if (std::abs(lhs.start - rhs.start) > 1.0e-6) return lhs.start < rhs.start;
                      return lhs.end < rhs.end;
                  });
        // Shared polygon edges often describe the same doorway from both
        // adjacent rooms. Merge those duplicate/overlapping intervals before
        // generating wall pieces so no hidden duplicate wall closes the door.
        std::vector<OpeningInterval> mergedOpenings;
        for (const OpeningInterval& opening : openings)
        {
            if (!mergedOpenings.empty() &&
                opening.start <= mergedOpenings.back().end + 0.001 &&
                std::abs(opening.bottomZ - mergedOpenings.back().bottomZ) < 0.05 &&
                std::abs(opening.topZ - mergedOpenings.back().topZ) < 0.05 &&
                ((opening.sectorA == mergedOpenings.back().sectorA &&
                  opening.sectorB == mergedOpenings.back().sectorB) ||
                 (opening.sectorA == mergedOpenings.back().sectorB &&
                  opening.sectorB == mergedOpenings.back().sectorA)))
            {
                mergedOpenings.back().end = std::max(mergedOpenings.back().end, opening.end);
                mergedOpenings.back().bottomZ =
                    std::max(mergedOpenings.back().bottomZ, opening.bottomZ);
                mergedOpenings.back().topZ =
                    std::min(mergedOpenings.back().topZ, opening.topZ);
            }
            else
            {
                mergedOpenings.push_back(opening);
            }
        }

        double cursor = 0.0;
        for (std::size_t openingIndex = 0; openingIndex < mergedOpenings.size(); ++openingIndex)
        {
            const OpeningInterval& opening = mergedOpenings[openingIndex];
            const double start = std::max(cursor, opening.start);
            const double end = std::max(start, opening.end);
            char portalSectorA = opening.sectorA;
            char portalSectorB = opening.sectorB;
            if (portalSectorA != '\0' && portalSectorB == '\0' &&
                end > start + 0.0005)
            {
                // Generated stair treads overlap their owning room rather than
                // cutting the room polygon into pieces.  At the first/last
                // tread an opening can therefore have only one literal edge
                // contribution.  Sample both sides to recover the underlying
                // neighbouring sector and keep the ceiling/sky traversal
                // continuous across that nested boundary.
                const double portalMid = (start + end) * 0.5;
                const double px = a[0] + (b[0] - a[0]) * portalMid;
                const double py = a[1] + (b[1] - a[1]) * portalMid;
                const double vx = b[0] - a[0];
                const double vy = b[1] - a[1];
                const double length = std::hypot(vx, vy);
                if (length > 0.001)
                {
                    const double nx = -vy / length * 0.02;
                    const double ny = vx / length * 0.02;
                    const PolygonSectorRegion* sideA =
                        polygonRegionAtWorld(px + nx, py + ny);
                    const PolygonSectorRegion* sideB =
                        polygonRegionAtWorld(px - nx, py - ny);
                    const char symbolA = sideA ? sideA->sector : '\0';
                    const char symbolB = sideB ? sideB->sector : '\0';
                    if (symbolA == portalSectorA && symbolB != '\0' &&
                        symbolB != portalSectorA)
                        portalSectorB = symbolB;
                    else if (symbolB == portalSectorA && symbolA != '\0' &&
                             symbolA != portalSectorA)
                        portalSectorB = symbolA;
                }
            }
            if (portalSectorA != '\0' && portalSectorB != '\0' &&
                portalSectorA != portalSectorB &&
                end > start + 0.0005)
            {
                VectorPortalDef portal;
                portal.x0 = a[0] + (b[0] - a[0]) * start;
                portal.y0 = a[1] + (b[1] - a[1]) * start;
                portal.x1 = a[0] + (b[0] - a[0]) * end;
                portal.y1 = a[1] + (b[1] - a[1]) * end;
                portal.bottomZ = opening.bottomZ;
                portal.topZ = opening.topZ;
                portal.sectorA = portalSectorA;
                portal.sectorB = portalSectorB;
                m_polygonPortals.push_back(portal);
            }
            if (start > cursor + 0.0005)
                appendWall("poly_wall_" + std::to_string(wallCounter++), region,
                           a, b, cursor, start, floorZ, wallTop);
            if (opening.bottomZ > floorZ + 0.01)
                appendWall("poly_sill_" + std::to_string(wallCounter++), region,
                           a, b, start, end, floorZ, std::min(opening.bottomZ, wallTop),
                           false);
            if (opening.topZ < wallTop - 0.01)
                appendWall("poly_lintel_" + std::to_string(wallCounter++), region,
                           a, b, start, end, std::max(opening.topZ, floorZ), wallTop, false);
            cursor = std::max(cursor, end);
        }
        if (cursor < 1.0 - 0.0005)
            appendWall("poly_wall_" + std::to_string(wallCounter++), region,
                       a, b, cursor, 1.0, floorZ, wallTop);
    }

    // T-junction normalization can discover the same logical portal from two
    // almost identical subsegments (for example the courtyard opening and the
    // first stair tread).  Leaving both entries in the traversal list lets a
    // ray step A -> Q and immediately Q -> A at practically the same distance.
    // Collapse those duplicates once, after all edge groups have been built.
    std::vector<VectorPortalDef> uniquePortals;
    uniquePortals.reserve(m_polygonPortals.size());
    auto samePoint = [](double ax, double ay, double bx, double by) {
        return std::hypot(ax - bx, ay - by) <= 0.006;
    };
    for (const VectorPortalDef& portal : m_polygonPortals)
    {
        bool merged = false;
        for (VectorPortalDef& existing : uniquePortals)
        {
            const bool samePair =
                (existing.sectorA == portal.sectorA &&
                 existing.sectorB == portal.sectorB) ||
                (existing.sectorA == portal.sectorB &&
                 existing.sectorB == portal.sectorA);
            if (!samePair)
                continue;

            const bool sameDirection =
                samePoint(existing.x0, existing.y0, portal.x0, portal.y0) &&
                samePoint(existing.x1, existing.y1, portal.x1, portal.y1);
            const bool oppositeDirection =
                samePoint(existing.x0, existing.y0, portal.x1, portal.y1) &&
                samePoint(existing.x1, existing.y1, portal.x0, portal.y0);
            if (!sameDirection && !oppositeDirection)
                continue;

            existing.bottomZ = std::max(existing.bottomZ, portal.bottomZ);
            existing.topZ = std::min(existing.topZ, portal.topZ);
            merged = true;
            break;
        }
        if (!merged)
            uniquePortals.push_back(portal);
    }
    m_polygonPortals = std::move(uniquePortals);
}

char BuildInteriorEngine::sectorSymbolAtWorld(double worldX, double worldY) const
{
    if (!m_vectorSectorLookup.empty())
    {
        const int sx = static_cast<int>(std::floor(
            (worldX - m_vectorLookupOriginX) * kVectorSectorSubdiv));
        const int sy = static_cast<int>(std::floor(
            (worldY - m_vectorLookupOriginY) * kVectorSectorSubdiv));
        if (sx >= 0 && sy >= 0 && sx < m_vectorSectorLookupW && sy < m_vectorSectorLookupH)
        {
            // Return the cached value even when it is zero. Falling back to an
            // exact point-in-polygon scan for every empty sub-cell was the main
            // performance regression on the new Houska polygon map.
            const char cached = m_vectorSectorLookup[
                static_cast<std::size_t>(sy) * m_vectorSectorLookupW + sx];
            if (cached != '\0')
                return cached;
            return sectorSymbolAt(static_cast<int>(std::floor(worldX)),
                                  static_cast<int>(std::floor(worldY)));
        }

        // Outside the precomputed vector bounds use the compatibility grid; do
        // not scan every polygon again.
        return sectorSymbolAt(static_cast<int>(std::floor(worldX)),
                              static_cast<int>(std::floor(worldY)));
    }

    if (const PolygonSectorRegion* region = polygonRegionAtWorld(worldX, worldY))
        return region->sector;

    return sectorSymbolAt(static_cast<int>(std::floor(worldX)),
                          static_cast<int>(std::floor(worldY)));
}

const BuildInteriorEngine::SectorDef& BuildInteriorEngine::sectorAtWorld(double worldX, double worldY) const
{
    const char symbol = sectorSymbolAtWorld(worldX, worldY);
    const SectorDef* found = m_sectorLookup[static_cast<unsigned char>(symbol)];
    if (found) return *found;
    return sectorAt(static_cast<int>(std::floor(worldX)), static_cast<int>(std::floor(worldY)));
}

const BuildInteriorEngine::SectorDef& BuildInteriorEngine::sectorAtPlayer() const
{
    return sectorAtWorld(m_posX, m_posY);
}


double BuildInteriorEngine::floorHeightAt(const SectorDef& sector, double worldX, double worldY) const
{
    return sector.floorHeight +
           sector.floorSlopeX * (worldX - sector.slopeOriginX) +
           sector.floorSlopeY * (worldY - sector.slopeOriginY);
}

double BuildInteriorEngine::ceilingHeightAt(const SectorDef& sector, double worldX, double worldY) const
{
    return sector.ceilingHeight +
           sector.ceilingSlopeX * (worldX - sector.slopeOriginX) +
           sector.ceilingSlopeY * (worldY - sector.slopeOriginY);
}

double BuildInteriorEngine::floorHeightAtWorld(double worldX, double worldY) const
{
    double traversalHeight = 0.0;
    if (traversalHeightAtWorld(worldX, worldY, traversalHeight))
        return traversalHeight;
    const SectorDef& sector = sectorAtWorld(worldX, worldY);
    return floorHeightAt(sector, worldX, worldY);
}

double BuildInteriorEngine::ceilingHeightAtWorld(double worldX, double worldY) const
{
    const SectorDef& sector = sectorAtWorld(worldX, worldY);
    return ceilingHeightAt(sector, worldX, worldY);
}

bool BuildInteriorEngine::traversalHeightAtWorld(
    double worldX, double worldY, double& height) const
{
    // Later definitions win, mirroring polygon-sector ownership.
    for (auto it = m_traversalRamps.rbegin();
         it != m_traversalRamps.rend(); ++it)
    {
        const double runX = it->endX - it->startX;
        const double runY = it->endY - it->startY;
        const double length = std::hypot(runX, runY);
        if (length <= 0.10)
            continue;
        const double dirX = runX / length;
        const double dirY = runY / length;
        const double relX = worldX - it->startX;
        const double relY = worldY - it->startY;
        const double along = relX * dirX + relY * dirY;
        const double lateral = -relX * dirY + relY * dirX;
        constexpr double endTolerance = 0.20;
        if (along < -endTolerance || along > length + endTolerance ||
            std::abs(lateral) > it->width * 0.5 + 0.015)
            continue;
        const double amount =
            std::clamp(along / length, 0.0, 1.0);
        height = it->startHeight +
            (it->endHeight - it->startHeight) * amount;
        return true;
    }
    return false;
}

bool BuildInteriorEngine::wallSegmentBlocksPoint(const WallSegmentDef& wall,
                                                   double x, double y, double radius) const
{
    if (!wall.solid) return false;
    const double vx = wall.x1 - wall.x0;
    const double vy = wall.y1 - wall.y0;
    const double lengthSq = vx * vx + vy * vy;
    if (lengthSq <= 1.0e-10) return false;
    const double t = std::clamp(((x - wall.x0) * vx + (y - wall.y0) * vy) / lengthSq, 0.0, 1.0);
    const double nx = wall.x0 + vx * t;
    const double ny = wall.y0 + vy * t;
    const double dx = x - nx;
    const double dy = y - ny;
    return dx * dx + dy * dy < radius * radius;
}

double BuildInteriorEngine::wallBottomAt(const WallSegmentDef& wall,
                                         double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    return wall.bottomZ +
        (wall.bottomZEnd - wall.bottomZ) * amount;
}

double BuildInteriorEngine::wallTopAt(const WallSegmentDef& wall,
                                      double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (!wall.topProfile.empty())
    {
        const std::size_t index = std::min(
            wall.topProfile.size() - 1,
            static_cast<std::size_t>(
                std::floor(amount * wall.topProfile.size())));
        return wall.topProfile[index];
    }
    return wall.topZ +
        (wall.topZEnd - wall.topZ) * amount;
}

const BuildInteriorEngine::WallSegmentDef* BuildInteriorEngine::nearestWallSegmentHit(
    double originX, double originY, double rayX, double rayY,
    double& distance, double& textureU) const
{
    const WallSegmentDef* best = nullptr;
    distance = kHuge;
    textureU = 0.0;

    auto testWalls = [&](const std::vector<WallSegmentDef>& walls)
    {
        for (const WallSegmentDef& wall : walls)
        {
            const double sx = wall.x1 - wall.x0;
            const double sy = wall.y1 - wall.y0;
            const double denominator = rayX * sy - rayY * sx;
            if (std::abs(denominator) < 1.0e-10) continue;
            const double qx = wall.x0 - originX;
            const double qy = wall.y0 - originY;
            const double t = (qx * sy - qy * sx) / denominator;
            const double u = (qx * rayY - qy * rayX) / denominator;
            constexpr double kJointEpsilon = 0.0035;
            if (t <= 0.02 || u < -kJointEpsilon || u > 1.0 + kJointEpsilon || t >= distance)
                continue;
            if (!wall.twoSided)
            {
                const double normalX = sy;
                const double normalY = -sx;
                if (normalX * rayX + normalY * rayY >= 0.0) continue;
            }
            best = &wall;
            distance = t;
            textureU = wall.textureUOffset +
                std::clamp(u, 0.0, 1.0) *
                    std::hypot(sx, sy) * wall.textureScale;
        }
    };

    testWalls(m_wallSegments);
    testWalls(m_polygonBoundaryWalls);
    return best;
}

bool BuildInteriorEngine::surfaceVisibleAlongRay(double worldX, double worldY,
                                                   double worldZ, double eyeZ) const
{
    const double dx = worldX - m_posX;
    const double dy = worldY - m_posY;
    const double totalDistance = std::hypot(dx, dy);
    if (totalDistance <= 0.02) return true;
    const double rayX = dx / totalDistance;
    const double rayY = dy / totalDistance;

    auto wallListOccludes = [&](const std::vector<WallSegmentDef>& walls)
    {
        for (const WallSegmentDef& wall : walls)
        {
            const double sx = wall.x1 - wall.x0;
            const double sy = wall.y1 - wall.y0;
            const double denominator = rayX * sy - rayY * sx;
            if (std::abs(denominator) < 1.0e-10)
                continue;

            const double qx = wall.x0 - m_posX;
            const double qy = wall.y0 - m_posY;
            const double distance = (qx * sy - qy * sx) / denominator;
            const double u = (qx * rayY - qy * rayX) / denominator;
            constexpr double kJointEpsilon = 0.0035;
            if (distance <= 0.02 ||
                distance >= totalDistance - 0.01 ||
                u < -kJointEpsilon ||
                u > 1.0 + kJointEpsilon)
                continue;
            if (!wall.twoSided)
            {
                const double normalX = sy;
                const double normalY = -sx;
                if (normalX * rayX + normalY * rayY >= 0.0)
                    continue;
            }

            const double amount = std::clamp(u, 0.0, 1.0);
            const double z = eyeZ +
                (worldZ - eyeZ) * (distance / totalDistance);
            if (z > wallBottomAt(wall, amount) + 0.01 &&
                z < wallTopAt(wall, amount) - 0.01)
                return true;
        }
        return false;
    };

    if (wallListOccludes(m_wallSegments) ||
        wallListOccludes(m_polygonBoundaryWalls))
        return false;

    if (hasVectorGeometry())
        return true;

    int mapX = static_cast<int>(std::floor(m_posX));
    int mapY = static_cast<int>(std::floor(m_posY));
    const double deltaX = std::abs(rayX) < 1.0e-12 ? kHuge : std::abs(1.0 / rayX);
    const double deltaY = std::abs(rayY) < 1.0e-12 ? kHuge : std::abs(1.0 / rayY);
    const int stepX = rayX < 0.0 ? -1 : 1;
    const int stepY = rayY < 0.0 ? -1 : 1;
    double sideX = rayX < 0.0 ? (m_posX - mapX) * deltaX : (mapX + 1.0 - m_posX) * deltaX;
    double sideY = rayY < 0.0 ? (m_posY - mapY) * deltaY : (mapY + 1.0 - m_posY) * deltaY;
    const SectorDef* currentSector = &sectorAt(mapX, mapY);

    for (int step = 0; step < 96; ++step)
    {
        double boundaryDistance = 0.0;
        if (sideX < sideY)
        {
            boundaryDistance = sideX;
            sideX += deltaX;
            mapX += stepX;
        }
        else
        {
            boundaryDistance = sideY;
            sideY += deltaY;
            mapY += stepY;
        }
        if (boundaryDistance >= totalDistance - 0.01) return true;
        if (!isInside(mapX, mapY) || isSolidCell(mapX, mapY)) return false;

        const SectorDef* nextSector = &sectorAt(mapX, mapY);
        if (nextSector->symbol != currentSector->symbol)
        {
            const double bx = m_posX + rayX * boundaryDistance;
            const double by = m_posY + rayY * boundaryDistance;
            const double currentFloorZ = floorHeightAt(*currentSector, bx, by);
            const double nextFloorZ = floorHeightAt(*nextSector, bx, by);
            const double currentCeilingZ = ceilingHeightAt(*currentSector, bx, by);
            const double nextCeilingZ = ceilingHeightAt(*nextSector, bx, by);
            const bool floorContinuous =
                std::abs(currentFloorZ - nextFloorZ) <= kPortalFloorContinuityEpsilon;
            const bool ceilingContinuous =
                std::abs(currentCeilingZ - nextCeilingZ) <= kPortalCeilingContinuityEpsilon ||
                (currentSector->skyCeiling && nextSector->skyCeiling);
            const double openingBottom = floorContinuous
                ? std::min(currentFloorZ, nextFloorZ)
                : std::max(currentFloorZ, nextFloorZ);
            const double openingTop = ceilingContinuous
                ? std::max(currentCeilingZ, nextCeilingZ)
                : std::min(currentCeilingZ, nextCeilingZ);
            const double z = eyeZ + (worldZ - eyeZ) * (boundaryDistance / totalDistance);
            if (openingTop <= openingBottom + 0.02 ||
                (!floorContinuous && z <= openingBottom + 0.01) ||
                z >= openingTop - 0.01)
                return false;
            currentSector = nextSector;
        }
    }
    return true;
}

BuildInteriorEngine::DoorMotion BuildInteriorEngine::parseDoorMotion(const std::string& value)
{
    if (value == "swing" || value == "hinged" || value == "normal")
        return DoorMotion::Swing;
    if (value == "slide" || value == "sliding")
        return DoorMotion::Slide;
    if (value == "raise" || value == "vertical" || value == "build")
        return DoorMotion::Raise;
    if (value == "transition" || value == "portal" || value == "use")
        return DoorMotion::Transition;
    return DoorMotion::Swing;
}

const char* BuildInteriorEngine::doorMotionName(DoorMotion motion)
{
    switch (motion)
    {
        case DoorMotion::Swing: return "swing";
        case DoorMotion::Slide: return "slide";
        case DoorMotion::Raise: return "raise";
        case DoorMotion::Transition: return "transition";
        default: return "swing";
    }
}

bool BuildInteriorEngine::doorCoversCell(const DoorDef& door, int x, int y) const
{
    if (door.axis == 'y')
        return x == door.x && y >= door.y && y < door.y + door.span;
    return y == door.y && x >= door.x && x < door.x + door.span;
}

double BuildInteriorEngine::doorCenterX(const DoorDef& door) const
{
    if (door.hasSegment)
        return (door.segmentX0 + door.segmentX1) * 0.5;
    return door.x + (door.axis == 'x' ? door.span * 0.5 : 0.5);
}

double BuildInteriorEngine::doorCenterY(const DoorDef& door) const
{
    if (door.hasSegment)
        return (door.segmentY0 + door.segmentY1) * 0.5;
    return door.y + (door.axis == 'y' ? door.span * 0.5 : 0.5);
}

double BuildInteriorEngine::doorTextureU(const DoorDef& door, int mapX, int mapY, double wallU) const
{
    if (door.span <= 1)
        return wallU;
    const int offset = door.axis == 'y' ? (mapY - door.y) : (mapX - door.x);
    return std::clamp((offset + wallU) / static_cast<double>(door.span), 0.0, 1.0);
}

char BuildInteriorEngine::doorLeafAxis(const DoorDef& door) const
{
    if (door.hasSegment)
    {
        return std::abs(door.segmentX1 - door.segmentX0) >=
               std::abs(door.segmentY1 - door.segmentY0) ? 'x' : 'y';
    }
    if (door.span > 1)
        return door.axis;

    auto wallLike = [&](int x, int y) {
        if (!isInside(x, y)) return true;
        const TextureKey cell = cellAt(x, y);
        return !isEmptyCell(cell) && cell != static_cast<TextureKey>('D');
    };
    const int horizontal = static_cast<int>(wallLike(door.x - 1, door.y)) +
                           static_cast<int>(wallLike(door.x + 1, door.y));
    const int vertical = static_cast<int>(wallLike(door.x, door.y - 1)) +
                         static_cast<int>(wallLike(door.x, door.y + 1));
    if (vertical > horizontal)
        return 'y';
    if (horizontal > vertical)
        return 'x';
    return door.axis;
}

void BuildInteriorEngine::doorLeafSegment(const DoorDef& door, double& x0, double& y0,
                                               double& x1, double& y1) const
{
    if (door.hasSegment)
    {
        x0 = door.segmentX0;
        y0 = door.segmentY0;
        x1 = door.segmentX1;
        y1 = door.segmentY1;
    }
    else
    {
        const char leafAxis = doorLeafAxis(door);
        if (leafAxis == 'y')
        {
            x0 = door.x + 0.5;
            y0 = static_cast<double>(door.y);
            x1 = door.x + 0.5;
            y1 = door.y + static_cast<double>(door.span);
        }
        else
        {
            x0 = static_cast<double>(door.x);
            y0 = door.y + 0.5;
            x1 = door.x + static_cast<double>(door.span);
            y1 = door.y + 0.5;
        }
    }

    if (door.hingeAtEnd)
    {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }

    const double vx = x1 - x0;
    const double vy = y1 - y0;
    if (door.motion == DoorMotion::Swing)
    {
        const double angle =
            door.swingDirection * door.openAmount * door.swingDegrees * kPi / 180.0;
        const double ca = std::cos(angle);
        const double sa = std::sin(angle);
        x1 = x0 + vx * ca - vy * sa;
        y1 = y0 + vx * sa + vy * ca;
    }
    else if (door.motion == DoorMotion::Slide)
    {
        // Slide the visible/collidable edge into the adjacent wall. Shortening
        // the segment matches the legacy texture reveal while remaining valid
        // for an arbitrarily angled doorway.
        x0 += vx * door.openAmount;
        y0 += vy * door.openAmount;
    }
}

void BuildInteriorEngine::doorVerticalBounds(const DoorDef& door,
                                             double& bottomZ,
                                             double& topZ) const
{
    const double doorwayX = door.hasSegment
        ? (door.segmentX0 + door.segmentX1) * 0.5
        : doorCenterX(door);
    const double doorwayY = door.hasSegment
        ? (door.segmentY0 + door.segmentY1) * 0.5
        : doorCenterY(door);
    const SectorDef& sector = sectorAtWorld(doorwayX, doorwayY);
    bottomZ = floorHeightAt(sector, doorwayX, doorwayY);
    const double ceilingZ = ceilingHeightAt(sector, doorwayX, doorwayY);
    const double requestedHeight = door.height > 0.0
        ? door.height
        : (ceilingZ - bottomZ);

    // wallHeight describes low side/riser faces of generated stair sectors.
    // It must not flatten a full-height leaf that happens to stand on a tread.
    topZ = std::clamp(
        bottomZ + requestedHeight,
        bottomZ + 0.35,
        std::max(bottomZ + 0.35, ceilingZ));
}

void BuildInteriorEngine::doorRevealDepths(
    const DoorDef& door,
    double& negativeDepth,
    double& positiveDepth,
    char& negativeSector,
    char& positiveSector) const
{
    negativeDepth = 0.0;
    positiveDepth = 0.0;
    negativeSector = '\0';
    positiveSector = '\0';
    if (!door.hasSegment)
        return;

    const double segmentX = door.segmentX1 - door.segmentX0;
    const double segmentY = door.segmentY1 - door.segmentY0;
    const double segmentLength = std::hypot(segmentX, segmentY);
    if (segmentLength <= 0.05)
        return;

    const double normalX = -segmentY / segmentLength;
    const double normalY = segmentX / segmentLength;
    const double middleX =
        (door.segmentX0 + door.segmentX1) * 0.5;
    const double middleY =
        (door.segmentY0 + door.segmentY1) * 0.5;

    auto probe = [&](double sign, double& depth, char& symbol)
    {
        constexpr double sampleStep = 0.025;
        constexpr double maximumDepth = 1.60;
        double entryDistance = -1.0;
        const PolygonSectorRegion* entryRegion = nullptr;

        for (double distance = sampleStep;
             distance <= maximumDepth + 1.0e-6;
             distance += sampleStep)
        {
            const PolygonSectorRegion* region = polygonRegionAtWorld(
                middleX + normalX * sign * distance,
                middleY + normalY * sign * distance);
            if (!region)
                continue;
            entryRegion = region;
            symbol = region->sector;
            entryDistance = distance;
            break;
        }

        if (symbol == '\0')
        {
            const SectorDef& fallback = sectorAtWorld(
                middleX + normalX * sign * 0.08,
                middleY + normalY * sign * 0.08);
            symbol = fallback.symbol;
            if (!fallback.skyCeiling)
                depth = 0.46;
            return;
        }

        const SectorDef* sector =
            m_sectorLookup[static_cast<unsigned char>(symbol)];
        if (!sector || sector->skyCeiling)
        {
            depth = 0.0;
            return;
        }

        if (entryRegion &&
            entryRegion->id.find("_connector") != std::string::npos)
        {
            for (double distance = entryDistance + sampleStep;
                 distance <= maximumDepth + 1.0e-6;
                 distance += sampleStep)
            {
                const PolygonSectorRegion* region =
                    polygonRegionAtWorld(
                        middleX + normalX * sign * distance,
                        middleY + normalY * sign * distance);
                if (!region || region->id != entryRegion->id)
                {
                    depth = std::min(
                        maximumDepth, distance + 0.015);
                    return;
                }
            }
            depth = maximumDepth;
            return;
        }

        // If the covered room begins immediately at the door plane, a normal
        // 46 cm reveal is enough.  Detached polygon rooms in the Houska data
        // intentionally leave a one-metre masonry band between their outline
        // and the courtyard wall; in that case the soffit must reach all the
        // way to the room boundary instead of stopping halfway through it.
        depth = entryDistance <= 0.08
            ? 0.46
            : std::min(maximumDepth, entryDistance + 0.04);
    };

    probe(-1.0, negativeDepth, negativeSector);
    probe(1.0, positiveDepth, positiveSector);
}

bool BuildInteriorEngine::doorBlocksPoint(const DoorDef& door, double x, double y, double radius) const
{
    if (door.motion == DoorMotion::Raise && door.openAmount >= 0.92)
        return false;
    if (door.motion == DoorMotion::Slide && door.openAmount >= 0.995)
        return false;
    if (!door.hasSegment && door.motion != DoorMotion::Swing)
        return false;

    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    doorLeafSegment(door, x0, y0, x1, y1);
    const double vx = x1 - x0;
    const double vy = y1 - y0;
    const double lengthSq = vx * vx + vy * vy;
    double t = 0.0;
    if (lengthSq > 1.0e-10)
        t = std::clamp(((x - x0) * vx + (y - y0) * vy) / lengthSq, 0.0, 1.0);
    const double nearestX = x0 + vx * t;
    const double nearestY = y0 + vy * t;
    const double dx = x - nearestX;
    const double dy = y - nearestY;
    const double clearance = radius + door.thickness * 0.5;
    return dx * dx + dy * dy < clearance * clearance;
}

BuildInteriorEngine::DoorDef* BuildInteriorEngine::doorAt(int x, int y)
{
    for (DoorDef& d : m_doors)
        if (doorCoversCell(d, x, y)) return &d;
    return nullptr;
}

const BuildInteriorEngine::DoorDef* BuildInteriorEngine::doorAt(int x, int y) const
{
    for (const DoorDef& d : m_doors)
        if (doorCoversCell(d, x, y)) return &d;
    return nullptr;
}

BuildInteriorEngine::DoorDef* BuildInteriorEngine::nearestUsableDoor(double maxDistance)
{
    const double fx = std::cos(m_angle);
    const double fy = std::sin(m_angle);
    DoorDef* best = nullptr;
    double bestDistance = maxDistance;
    for (DoorDef& d : m_doors)
    {
        const double dx = doorCenterX(d) - m_posX;
        const double dy = doorCenterY(d) - m_posY;
        const double distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= 0.001 || distance > bestDistance) continue;
        const double facing = (dx * fx + dy * fy) / distance;
        if (facing < 0.15) continue;
        best = &d;
        bestDistance = distance;
    }
    return best;
}

BuildInteriorEngine::SpriteDef* BuildInteriorEngine::nearestUsableSprite(double maxDistance)
{
    const double fx = std::cos(m_angle);
    const double fy = std::sin(m_angle);
    SpriteDef* best = nullptr;
    double bestDistance = maxDistance;
    for (SpriteDef& s : m_sprites)
    {
        if (s.interactionLabel.empty() && s.targetInterior.empty()) continue;
        const double dx = s.x - m_posX;
        const double dy = s.y - m_posY;
        const double distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= 0.001 || distance > bestDistance) continue;
        const double facing = (dx * fx + dy * fy) / distance;
        if (facing < 0.35) continue;
        best = &s;
        bestDistance = distance;
    }
    return best;
}

bool BuildInteriorEngine::isDoorOpenEnough(int x, int y) const
{
    const DoorDef* d = doorAt(x, y);
    if (!d || d->motion == DoorMotion::Transition)
        return false;
    return d->openAmount >= 0.92;
}

bool BuildInteriorEngine::isSolidCell(int x, int y) const
{
    if (!isInside(x, y)) return true;
    const TextureKey cell = cellAt(x, y);
    if (cell == static_cast<TextureKey>('D'))
    {
        const DoorDef* door = doorAt(x, y);
        // A hinged leaf has a line/box collider instead of blocking the whole
        // grid cell. canStandAt() checks its actual rotated segment.
        if (door && door->motion == DoorMotion::Swing)
            return false;
        return !isDoorOpenEnough(x, y);
    }
    return !isEmptyCell(cell);
}

bool BuildInteriorEngine::spriteBlocks(double x, double y) const
{
    for (const SpriteDef& s : m_sprites)
    {
        if (!s.solid) continue;
        const TextureRef* texture = m_textureLookup[static_cast<std::size_t>(s.texture)];
        const double aspect = texture && texture->h > 0
            ? static_cast<double>(texture->w) / texture->h
            : 1.0;
        const double halfWidth = std::max(0.14, s.scale * aspect * 0.5);
        const double depth = s.renderMode == ObjectRenderMode::Voxel
            ? (s.voxelDepth > 0.0 ? s.voxelDepth : std::max(0.14, s.scale * 0.32))
            : 0.22;
        const double halfDepth = depth * 0.5;
        const double dx = x - s.x;
        const double dy = y - s.y;
        const double ca = std::cos(s.yaw);
        const double sa = std::sin(s.yaw);
        const double localX = ca * dx + sa * dy;
        const double localY = -sa * dx + ca * dy;
        if (std::abs(localX) < halfWidth + kPlayerRadius &&
            std::abs(localY) < halfDepth + kPlayerRadius)
            return true;
    }
    return false;
}

bool BuildInteriorEngine::canStandAt(double x, double y) const
{
    if (!std::isfinite(x) || !std::isfinite(y))
        return false;

    for (const DoorDef& door : m_doors)
    {
        if (doorBlocksPoint(door, x, y, kPlayerRadius))
            return false;
    }
    for (const WallSegmentDef& wall : m_wallSegments)
    {
        if (wallSegmentBlocksPoint(wall, x, y, kPlayerRadius))
            return false;
    }
    for (const WallSegmentDef& wall : m_polygonBoundaryWalls)
    {
        if (wallSegmentBlocksPoint(wall, x, y, kPlayerRadius))
            return false;
    }

    const int centerX = static_cast<int>(std::floor(x));
    const int centerY = static_cast<int>(std::floor(y));
    if (hasVectorGeometry())
    {
        if (!polygonRegionAtWorld(x, y))
            return false;
    }
    else
    {
        if (!isInside(centerX, centerY) || isSolidCell(centerX, centerY))
            return false;
    }

    const double centerFloor = floorHeightAtWorld(x, y);
    const double centerCeiling = ceilingHeightAtWorld(x, y);
    if (centerCeiling - centerFloor < kPlayerBodyHeight + kMinimumHeadroom)
        return false;

    const double r = kPlayerRadius;
    const double points[4][2] = {{x-r,y-r},{x+r,y-r},{x-r,y+r},{x+r,y+r}};
    for (const auto& p : points)
    {
        const int tileX = static_cast<int>(std::floor(p[0]));
        const int tileY = static_cast<int>(std::floor(p[1]));
        if (hasVectorGeometry())
        {
            if (!polygonRegionAtWorld(p[0], p[1]))
                return false;
        }
        else
        {
            if (!isInside(tileX, tileY) || isSolidCell(tileX, tileY))
                return false;
        }

        const double cornerFloor = floorHeightAtWorld(p[0], p[1]);
        const double cornerCeiling = ceilingHeightAtWorld(p[0], p[1]);
        if (cornerCeiling - cornerFloor < kPlayerBodyHeight + kMinimumHeadroom)
            return false;

        // This remains tolerant of a smooth ramp under the collider, while
        // preventing the player from straddling a deep pit or high ledge.
        if (std::abs(cornerFloor - centerFloor) > kMaxStepUp)
            return false;
    }
    return !spriteBlocks(x, y);
}

bool BuildInteriorEngine::canMoveBetween(double fromX, double fromY,
                                         double toX, double toY) const
{
    if (!canStandAt(toX, toY))
        return false;

    const double fromFloor = floorHeightAtWorld(fromX, fromY);
    const double toFloor = floorHeightAtWorld(toX, toY);
    const double toCeiling = ceilingHeightAtWorld(toX, toY);

    if (!m_grounded)
    {
        // While airborne the feet may clear a ledge. The destination must still
        // have enough room for the body at the current jump height.
        if (toFloor > m_playerZ + kAirborneStepClearance)
            return false;
        if (toCeiling < m_playerZ + kPlayerBodyHeight + kMinimumHeadroom)
            return false;
        return true;
    }

    const double delta = toFloor - fromFloor;
    if (delta > kMaxStepUp)
        return false;
    if (delta < -kMaxStepDown)
        return false;

    return true;
}

void BuildInteriorEngine::tryMove(double dx, double dy)
{
    const double nextX = m_posX + dx;
    const double nextY = m_posY + dy;

    if (canMoveBetween(m_posX, m_posY, nextX, m_posY))
    {
        m_posX = nextX;
        if (m_grounded)
        {
            m_lastSafeX = m_posX;
            m_lastSafeY = m_posY;
        }
    }

    if (canMoveBetween(m_posX, m_posY, m_posX, nextY))
    {
        m_posY = nextY;
        if (m_grounded)
        {
            m_lastSafeX = m_posX;
            m_lastSafeY = m_posY;
        }
    }
}

void BuildInteriorEngine::recoverToLastSafePosition()
{
    if (canStandAt(m_lastSafeX, m_lastSafeY))
    {
        m_posX = m_lastSafeX;
        m_posY = m_lastSafeY;
        m_playerZ = floorHeightAtWorld(m_posX, m_posY);
        m_verticalVelocity = 0.0;
        m_grounded = true;
        m_playerZInitialized = true;
        m_cameraFloorZ = m_playerZ;
    }
    else
    {
        const auto spawnIt = m_namedSpawns.find("main_entry");
        if (spawnIt != m_namedSpawns.end() && canStandAt(spawnIt->second.x, spawnIt->second.y))
        {
            m_posX = spawnIt->second.x;
            m_posY = spawnIt->second.y;
            m_angle = spawnIt->second.angle;
            m_pitch = spawnIt->second.pitch;
        }
        else
        {
            for (int radius = 0; radius <= 16; ++radius)
            {
                bool found = false;
                for (int oy = -radius; oy <= radius && !found; ++oy)
                {
                    for (int ox = -radius; ox <= radius; ++ox)
                    {
                        const double candidateX = std::floor(m_posX) + ox + 0.5;
                        const double candidateY = std::floor(m_posY) + oy + 0.5;
                        if (canStandAt(candidateX, candidateY))
                        {
                            m_posX = candidateX;
                            m_posY = candidateY;
                            found = true;
                            break;
                        }
                    }
                }
                if (found) break;
            }
        }
    }

    m_lastSafeX = m_posX;
    m_lastSafeY = m_posY;
    m_playerZ = floorHeightAtWorld(m_posX, m_posY);
    m_verticalVelocity = 0.0;
    m_grounded = true;
    m_playerZInitialized = true;
    m_cameraFloorZ = m_playerZ;
    m_cameraFloorInitialized = true;
    m_status = U8("Pozice kamery obnovena.");
}

void BuildInteriorEngine::sanitizeEditorState()
{
    ensureSectorGrid();

    if (m_editorSelectedDoor < 0 ||
        m_editorSelectedDoor >= static_cast<int>(m_doors.size()))
        m_editorSelectedDoor = -1;

    if (m_editorSelectedSprite < 0 ||
        m_editorSelectedSprite >= static_cast<int>(m_sprites.size()))
        m_editorSelectedSprite = -1;

    if (m_editorSelectedPolygon < 0 ||
        m_editorSelectedPolygon >= static_cast<int>(m_polygonSectors.size()))
    {
        m_editorSelectedPolygon = -1;
        m_editorSelectedPolygonVertex = -1;
        m_editorSelectedOpening = -1;
    }
    else
    {
        const PolygonSectorRegion& polygon = m_polygonSectors[static_cast<std::size_t>(m_editorSelectedPolygon)];
        if (m_editorSelectedPolygonVertex < 0 ||
            m_editorSelectedPolygonVertex >= static_cast<int>(polygon.vertices.size()))
            m_editorSelectedPolygonVertex = -1;
        if (m_editorSelectedOpening < 0 ||
            m_editorSelectedOpening >= static_cast<int>(polygon.openings.size()))
            m_editorSelectedOpening = -1;
    }

    if (m_editorSectorBrush < 'A' || m_editorSectorBrush > 'Z')
        m_editorSectorBrush = 'A';
    if (m_editorStairFirstSector < 'A' || m_editorStairFirstSector > 'Z')
        m_editorStairFirstSector = 'H';

    m_editorMapCellSize = std::clamp(m_editorMapCellSize, 8.0f, 48.0f);
}

void BuildInteriorEngine::useNearestInteraction()
{
    DoorDef* door = nearestUsableDoor();
    SpriteDef* sprite = nearestUsableSprite();

    auto distanceToDoor = [&](const DoorDef* d) {
        if (!d) return kHuge;
        return std::hypot(doorCenterX(*d) - m_posX,
                          doorCenterY(*d) - m_posY);
    };
    auto distanceToSprite = [&](const SpriteDef* s) {
        if (!s) return kHuge;
        return std::hypot(s->x - m_posX, s->y - m_posY);
    };

    if (sprite && distanceToSprite(sprite) < distanceToDoor(door))
    {
        if (!sprite->targetInterior.empty())
        {
            const std::string target = sprite->targetInterior;
            if (requestCampaignTransitionIfNeeded(target, sprite->targetSpawn))
                return;
            if (!loadInteriorAtSpawn(target, sprite->targetSpawn))
                m_status = U8("Přechod se nezdařil: ") + target;
            return;
        }
        m_status = sprite->interactionLabel.empty() ? U8("Objekt nemá použití.") : sprite->interactionLabel;
        return;
    }

    toggleNearestDoor();
}

void BuildInteriorEngine::toggleNearestDoor()
{
    DoorDef* d = nearestUsableDoor();
    if (!d)
    {
        m_status = U8("Není tu nic k použití.");
        return;
    }
    if (d->locked)
    {
        m_status = U8("Dveře jsou zamčené.");
        return;
    }
    if (d->motion == DoorMotion::Transition || !d->targetInterior.empty())
    {
        if (d->targetInterior.empty())
        {
            m_status = U8("Brána zatím nemá nastavenou cílovou mapu.");
            return;
        }
        const std::string target = d->targetInterior;
        if (requestCampaignTransitionIfNeeded(target, d->targetSpawn))
            return;
        if (!loadInteriorAtSpawn(target, d->targetSpawn))
            m_status = U8("Nelze projít branou: ") + target;
        return;
    }
    const bool targetOpen = !d->targetOpen;
    const std::string group = d->interactionGroup.empty() ? d->id : d->interactionGroup;
    for (DoorDef& leaf : m_doors)
    {
        const std::string leafGroup =
            leaf.interactionGroup.empty() ? leaf.id : leaf.interactionGroup;
        if (leafGroup == group)
            leaf.targetOpen = targetOpen;
    }
    m_sceneDirty = true;
    m_status = targetOpen
        ? (d->motion == DoorMotion::Raise ? U8("Dveře se zvedají.")
           : d->motion == DoorMotion::Slide ? U8("Dveře se odsouvají.")
           : U8("Otevíráš dveře na pantu."))
        : (d->motion == DoorMotion::Raise ? U8("Dveře se spouštějí.")
           : d->motion == DoorMotion::Slide ? U8("Dveře se zasouvají.")
           : U8("Zavíráš dveře."));
}

void BuildInteriorEngine::updateDoors(float dt)
{
    for (DoorDef& d : m_doors)
    {
        if (d.motion == DoorMotion::Transition)
        {
            d.openAmount = 0.0;
            d.targetOpen = false;
            continue;
        }
        const double target = d.targetOpen ? 1.0 : 0.0;
        const double amount = d.speed * static_cast<double>(dt);
        const double before = d.openAmount;
        if (d.openAmount < target) d.openAmount = std::min(target, d.openAmount + amount);
        if (d.openAmount > target) d.openAmount = std::max(target, d.openAmount - amount);
        if (std::abs(d.openAmount - before) > 1.0e-8)
            m_sceneDirty = true;
    }
}


void BuildInteriorEngine::updateAnimatedEffects(float dt)
{
    bool changed = false;
    for (SpriteDef& sprite : m_sprites)
    {
        const TextureRef* texture = m_textureLookup[static_cast<std::size_t>(sprite.texture)];
        const int frameCount = texture ? static_cast<int>(texture->animationFrames.size()) : 0;
        if ((!sprite.animated || frameCount <= 1) && !sprite.emitsLight)
            continue;

        sprite.animationTimer -= static_cast<double>(dt);
        if (sprite.animationTimer > 0.0)
            continue;

        const double randomA = randomUnit(sprite.animationSeed);
        const double randomB = randomUnit(sprite.animationSeed);
        if (sprite.animated && frameCount > 1)
        {
            if (sprite.randomAnimation)
            {
                // Mostly progress forward, but occasionally skip or briefly step
                // backwards. This avoids the mechanical synchronized GIF look.
                int step = 1 + (nextRandom(sprite.animationSeed) % 2u == 0u ? 0 : 1);
                if (nextRandom(sprite.animationSeed) % 11u == 0u)
                    step = -1;
                sprite.animationFrame = (sprite.animationFrame + step + frameCount) % frameCount;
            }
            else
            {
                sprite.animationFrame = (sprite.animationFrame + 1) % frameCount;
            }
        }

        if (sprite.emitsLight)
        {
            const double signedNoise = randomB * 2.0 - 1.0;
            sprite.lightMultiplier = std::clamp(1.0 + signedNoise * sprite.lightFlicker,
                                                0.35, 1.75);
        }

        const double fps = sprite.animationMinFps +
            (sprite.animationMaxFps - sprite.animationMinFps) * randomA;
        const double timingJitter = 0.84 + randomB * 0.32;
        sprite.animationTimer += timingJitter / std::max(1.0, fps);
        changed = true;
    }

    if (changed)
        m_sceneDirty = true;
}

void BuildInteriorEngine::rebuildRenderLights()
{
    m_renderLights.clear();
    m_renderLights.reserve(m_sprites.size());
    for (const SpriteDef& light : m_sprites)
    {
        if (!light.emitsLight || light.lightRadius <= 0.01 || light.lightIntensity <= 0.001)
            continue;

        RuntimeLight cached;
        cached.source = &light;
        cached.x = light.x;
        cached.y = light.y;
        cached.z = floorHeightAtWorld(light.x, light.y) + light.zOffset + light.lightHeight;
        cached.radius = light.lightRadius;
        cached.radiusSquared = light.lightRadius * light.lightRadius;
        cached.intensity = light.lightIntensity * light.lightMultiplier;
        cached.r = light.lightR;
        cached.g = light.lightG;
        cached.b = light.lightB;
        cached.sector = sectorSymbolAtWorld(light.x, light.y);
        m_renderLights.push_back(cached);
    }
}

std::uint32_t BuildInteriorEngine::applyWorldLighting(std::uint32_t color, double baseShade,
                                                        double worldX, double worldY, double worldZ,
                                                        const SpriteDef* selfEmitter) const
{
    color = modulate(color, baseShade);
    if (!std::isfinite(worldX) || !std::isfinite(worldY) ||
        std::abs(worldX) > 1.0e20 || std::abs(worldY) > 1.0e20 ||
        m_renderLights.empty())
        return color;

    double r = static_cast<double>((color >> 16u) & 0xffu);
    double g = static_cast<double>((color >> 8u) & 0xffu);
    double b = static_cast<double>(color & 0xffu);
    const int a = static_cast<int>((color >> 24u) & 0xffu);
    char pointSector = '\0';
    bool pointSectorResolved = false;

    for (const RuntimeLight& light : m_renderLights)
    {
        if (light.source == selfEmitter)
            continue;

        const double dx = worldX - light.x;
        const double dy = worldY - light.y;
        const double dz = (worldZ - light.z) * 0.72;
        if (std::abs(dx) >= light.radius || std::abs(dy) >= light.radius ||
            std::abs(dz) >= light.radius)
            continue;

        const double distanceSquared = dx * dx + dy * dy + dz * dz;
        if (distanceSquared >= light.radiusSquared)
            continue;

        // Squared falloff avoids a sqrt() for every light and rendered pixel.
        double attenuation = 1.0 - distanceSquared / light.radiusSquared;
        attenuation *= attenuation * light.intensity;

        if (!pointSectorResolved)
        {
            pointSector = sectorSymbolAtWorld(worldX, worldY);
            pointSectorResolved = true;
        }
        if (pointSector != light.sector && distanceSquared > 1.3225)
            attenuation *= 0.28;

        const double amount = std::clamp(attenuation * 0.72, 0.0, 0.92);
        const double lr = light.r / 255.0;
        const double lg = light.g / 255.0;
        const double lb = light.b / 255.0;
        r += (255.0 - r) * lr * amount;
        g += (255.0 - g) * lg * amount;
        b += (255.0 - b) * lb * amount;
    }

    return argb(static_cast<int>(std::clamp(r, 0.0, 255.0)),
                static_cast<int>(std::clamp(g, 0.0, 255.0)),
                static_cast<int>(std::clamp(b, 0.0, 255.0)), a);
}

void BuildInteriorEngine::renderScene(int renderW, int renderH)
{
    const int screenW = renderW;
    const int screenH = renderH;
    ensureSceneBuffer(screenW, screenH);
    if (!m_sceneTexture) return;

    const double floorZ = m_cameraFloorInitialized ? m_cameraFloorZ
                                                    : floorHeightAtWorld(m_posX, m_posY);
    const double eyeZ = floorZ + m_eyeHeight;
    const double bob = std::sin(m_bobPhase) * 4.0 * m_moveBlend;
    const int horizon = std::clamp(static_cast<int>(screenH * (0.5 + m_pitch * 0.60) + bob),
                                   -screenH, screenH * 2);

    std::fill(m_framebuffer.begin(), m_framebuffer.end(), argb(10, 10, 12));
    std::fill(m_zBuffer.begin(), m_zBuffer.end(), kHuge);
    std::fill(m_dynamicDepthBuffer.begin(), m_dynamicDepthBuffer.end(), kHuge);
    rebuildRenderLights();
    renderFloorAndCeiling(screenW, screenH, horizon, eyeZ);
    renderDoorSoffits(screenW, screenH, horizon, eyeZ);
    renderPortalWalls(screenW, screenH, horizon, eyeZ);
    renderSwingDoors(screenW, screenH, horizon, eyeZ);
    renderSprites(screenW, screenH, horizon, eyeZ);

    SDL_UpdateTexture(m_sceneTexture, nullptr, m_framebuffer.data(), screenW * static_cast<int>(sizeof(std::uint32_t)));
    SDL_RenderCopy(m_renderer, m_sceneTexture, nullptr, nullptr);
}

void BuildInteriorEngine::renderFloorAndCeiling(int screenW, int screenH, int horizon, double eyeZ)
{
    if (hasVectorGeometry())
    {
        renderPolygonSurfaces(screenW, screenH, horizon, eyeZ);
        return;
    }

    const double dirX = std::cos(m_angle);
    const double dirY = std::sin(m_angle);
    const double planeScale = std::tan(m_fov * 0.5);
    const double planeX = -dirY * planeScale;
    const double planeY = dirX * planeScale;
    const double ray0X = dirX - planeX;
    const double ray0Y = dirY - planeY;
    const double ray1X = dirX + planeX;
    const double ray1Y = dirY + planeY;
    const SectorDef* playerSector = &sectorAtPlayer();
    const TextureRef* skyTexture = m_textureLookup[static_cast<std::size_t>('K')];
    const double skyAngleU = (m_angle + kPi) / (2.0 * kPi);
    const double skyFovPart = m_fov / (2.0 * kPi);

    struct PortalClipEvent
    {
        double distance = 0.0;
        double bottom = 0.0;
        double top = 0.0;
        char sectorA = '\0';
        char sectorB = '\0';
        bool floorContinuous = false;
        bool ceilingContinuous = false;
    };
    struct ColumnOcclusion
    {
        double solidDistance = kHuge;
        std::array<PortalClipEvent, 48> events{};
        int eventCount = 0;
    };
    // Fast mode samples two adjacent columns together. The renderer already
    // runs at a reduced internal resolution, so this is a better tradeoff for
    // exterior maps than forcing per-pixel floor/ceiling casts on every slope.
    std::vector<ColumnOcclusion> occlusion(static_cast<std::size_t>(screenW));
    const int occlusionStep = m_fastFloorCasting ? 2 : 1;
    for (int x = 0; x < screenW; x += occlusionStep)
    {
        const int blockWidth = std::min(occlusionStep, screenW - x);
        const double sampleX = x + (blockWidth - 1) * 0.5;
        const double t = screenW <= 1 ? 0.0 : sampleX / (screenW - 1);
        const double rayX = ray0X + (ray1X - ray0X) * t;
        const double rayY = ray0Y + (ray1Y - ray0Y) * t;
        ColumnOcclusion column;

        double arbitraryDistance = kHuge, arbitraryU = 0.0;
        nearestWallSegmentHit(m_posX, m_posY, rayX, rayY, arbitraryDistance, arbitraryU);

        if (hasVectorGeometry())
        {
            // Polygon walls are vertically split into jamb/sill/lintel pieces.
            // Treating their shared XY distance as an entirely solid column
            // made a lintel block the sky even when the camera ray travelled
            // through the open part of a doorway.  Store real portal apertures
            // here; the wall pass itself covers the solid pieces afterwards.
            column.solidDistance = kHuge;
            std::vector<PortalClipEvent> hits;
            hits.reserve(24);
            for (const VectorPortalDef& portal : m_polygonPortals)
            {
                const double sx = portal.x1 - portal.x0;
                const double sy = portal.y1 - portal.y0;
                const double denominator = rayX * sy - rayY * sx;
                if (std::abs(denominator) < 1.0e-10)
                    continue;
                const double qx = portal.x0 - m_posX;
                const double qy = portal.y0 - m_posY;
                const double distance = (qx * sy - qy * sx) / denominator;
                const double u = (qx * rayY - qy * rayX) / denominator;
                if (distance <= 0.015 || u < -0.002 || u > 1.002)
                    continue;
                hits.push_back({
                    distance,
                    portal.bottomZ,
                    portal.topZ,
                    portal.sectorA,
                    portal.sectorB,
                    false,
                    false
                });
            }
            std::sort(hits.begin(), hits.end(),
                      [](const PortalClipEvent& lhs, const PortalClipEvent& rhs) {
                          return lhs.distance < rhs.distance;
                      });
            for (const PortalClipEvent& hit : hits)
            {
                if (column.eventCount >= static_cast<int>(column.events.size()))
                    break;
                if (column.eventCount > 0)
                {
                    const PortalClipEvent& previous =
                        column.events[static_cast<std::size_t>(column.eventCount - 1)];
                    const bool samePair =
                        (previous.sectorA == hit.sectorA && previous.sectorB == hit.sectorB) ||
                        (previous.sectorA == hit.sectorB && previous.sectorB == hit.sectorA);
                    if (samePair &&
                        std::abs(previous.distance - hit.distance) < 0.002 &&
                        std::abs(previous.bottom - hit.bottom) < 0.02 &&
                        std::abs(previous.top - hit.top) < 0.02)
                        continue;
                }
                column.events[static_cast<std::size_t>(column.eventCount++)] = hit;
            }
            for (int dx = 0; dx < blockWidth; ++dx)
                occlusion[static_cast<std::size_t>(x + dx)] = column;
            continue;
        }

        int mapX = static_cast<int>(std::floor(m_posX));
        int mapY = static_cast<int>(std::floor(m_posY));
        const double deltaX = std::abs(rayX) < 1.0e-12 ? kHuge : std::abs(1.0 / rayX);
        const double deltaY = std::abs(rayY) < 1.0e-12 ? kHuge : std::abs(1.0 / rayY);
        const int stepX = rayX < 0.0 ? -1 : 1;
        const int stepY = rayY < 0.0 ? -1 : 1;
        double sideX = rayX < 0.0 ? (m_posX - mapX) * deltaX : (mapX + 1.0 - m_posX) * deltaX;
        double sideY = rayY < 0.0 ? (m_posY - mapY) * deltaY : (mapY + 1.0 - m_posY) * deltaY;
        const SectorDef* current = &sectorAtWorld(m_posX, m_posY);
        for (int step = 0; step < 96; ++step)
        {
            double distance = 0.0;
            if (sideX < sideY)
            {
                distance = sideX; sideX += deltaX; mapX += stepX;
            }
            else
            {
                distance = sideY; sideY += deltaY; mapY += stepY;
            }
            if (arbitraryDistance < distance - 0.001)
            {
                column.solidDistance = arbitraryDistance;
                break;
            }
            if (!isInside(mapX, mapY) || isSolidCell(mapX, mapY))
            {
                column.solidDistance = distance;
                break;
            }
            const double bx = m_posX + rayX * distance;
            const double by = m_posY + rayY * distance;
            const SectorDef* next = &sectorAtWorld(bx + rayX * 0.001, by + rayY * 0.001);
            if (next->symbol != current->symbol && column.eventCount < static_cast<int>(column.events.size()))
            {
                const double currentFloorZ = floorHeightAt(*current, bx, by);
                const double nextFloorZ = floorHeightAt(*next, bx, by);
                const double currentCeilingZ = ceilingHeightAt(*current, bx, by);
                const double nextCeilingZ = ceilingHeightAt(*next, bx, by);
                const bool floorContinuous =
                    std::abs(currentFloorZ - nextFloorZ) <= kPortalFloorContinuityEpsilon;
                const bool ceilingContinuous =
                    std::abs(currentCeilingZ - nextCeilingZ) <= kPortalCeilingContinuityEpsilon ||
                    (current->skyCeiling && next->skyCeiling);
                PortalClipEvent& event = column.events[column.eventCount++];
                event.distance = distance;
                event.bottom = floorContinuous
                    ? std::min(currentFloorZ, nextFloorZ)
                    : std::max(currentFloorZ, nextFloorZ);
                event.top = ceilingContinuous
                    ? std::max(currentCeilingZ, nextCeilingZ)
                    : std::min(currentCeilingZ, nextCeilingZ);
                event.sectorA = current->symbol;
                event.sectorB = next->symbol;
                event.floorContinuous = floorContinuous;
                event.ceilingContinuous = ceilingContinuous;
                current = next;
            }
        }
        for (int dx = 0; dx < blockWidth; ++dx) occlusion[static_cast<std::size_t>(x + dx)] = column;
    }

    auto solveDistance = [&](const SectorDef& sector, bool floorSurface,
                             double rayX, double rayY, double verticalStep) -> double
    {
        const double slopeX = floorSurface ? sector.floorSlopeX : sector.ceilingSlopeX;
        const double slopeY = floorSurface ? sector.floorSlopeY : sector.ceilingSlopeY;
        const double base = floorSurface ? sector.floorHeight : sector.ceilingHeight;
        const double atCamera = base +
            slopeX * (m_posX - sector.slopeOriginX) +
            slopeY * (m_posY - sector.slopeOriginY);
        const double raySlope = slopeX * rayX + slopeY * rayY;

        const double numerator = atCamera - eyeZ;
        const double denominator = verticalStep - raySlope;
        if (std::abs(denominator) < 1.0e-5)
            return -1.0;
        const double result = numerator / denominator;
        return std::isfinite(result) && result > 0.001 ? result : -1.0;
    };

    for (int y = 0; y < screenH; ++y)
    {
        if (y == horizon)
            continue;

        const double verticalStep =
            static_cast<double>(horizon - y) / std::max(1, screenH);
        const bool defaultFloorRow = verticalStep < 0.0;
        const int xStep = m_fastFloorCasting ? 2 : 1;

        // Sky vertical coordinate and haze depend only on the scanline. The old
        // code recalculated pow() for every sky pixel, which was especially
        // expensive on the exterior map.
        const int skyBottom = std::max(1, horizon);
        double skyV = std::clamp(static_cast<double>(y) / skyBottom, 0.0, 1.0);
        skyV = 0.02 + std::pow(skyV, 0.90) * 0.96;
        const double skyHaze = std::clamp((skyV - 0.68) / 0.30, 0.0, 1.0) * 0.24;
        const std::uint32_t skyHazeColor = argb(150, 162, 170, static_cast<int>(skyHaze * 255.0));

        for (int x = 0; x < screenW; x += xStep)
        {
            const int blockWidth = std::min(xStep, screenW - x);
            const double sampleX = x + (blockWidth - 1) * 0.5;
            const double t = screenW <= 1 ? 0.0 : sampleX / (screenW - 1);
            const double rayX = ray0X + (ray1X - ray0X) * t;
            const double rayY = ray0Y + (ray1Y - ray0Y) * t;
            const ColumnOcclusion& column = occlusion[static_cast<std::size_t>(x)];

            bool floorRow = defaultFloorRow;
            bool ceilingRow = !floorRow;
            if (ceilingRow)
            {
                // A sloped floor that rises away from the camera can project
                // above the flat-world horizon.  Treat that scanline as floor
                // when the floor plane is the first finite surface on the ray;
                // otherwise looking uphill leaves the ramp unrendered and the
                // sky/background cuts through the terrain.
                auto previewSurfaceDistance = [&](bool floorSurface)
                {
                    const SectorDef* current = playerSector;
                    double traversedDistance = 0.0;
                    for (int traversal = 0; traversal < 48 && current; ++traversal)
                    {
                        const bool unboundedSky =
                            !floorSurface && current->skyCeiling;
                        const double surfaceDistance = unboundedSky
                            ? -1.0
                            : solveDistance(*current, floorSurface,
                                            rayX, rayY, verticalStep);
                        const bool surfaceInCurrentSpan =
                            surfaceDistance > traversedDistance + 0.001;

                        const PortalClipEvent* nextPortal = nullptr;
                        char nextSymbol = '\0';
                        for (int eventIndex = 0; eventIndex < column.eventCount; ++eventIndex)
                        {
                            const PortalClipEvent& event = column.events[eventIndex];
                            if (event.distance <= traversedDistance + 0.002)
                                continue;
                            if (surfaceInCurrentSpan &&
                                event.distance >= surfaceDistance - 0.001)
                                break;
                            if (event.sectorA == current->symbol)
                                nextSymbol = event.sectorB;
                            else if (event.sectorB == current->symbol)
                                nextSymbol = event.sectorA;
                            else
                                continue;
                            nextPortal = &event;
                            break;
                        }

                        if (surfaceInCurrentSpan &&
                            (!nextPortal ||
                             surfaceDistance <= nextPortal->distance + 0.001))
                            return surfaceDistance;
                        if (!nextPortal)
                            break;

                        traversedDistance = nextPortal->distance;
                        current = m_sectorLookup[
                            static_cast<unsigned char>(nextSymbol)];
                    }
                    return -1.0;
                };
                const double floorPreview = previewSurfaceDistance(true);
                const double ceilingPreview = previewSurfaceDistance(false);
                if (floorPreview > 0.0 &&
                    (ceilingPreview <= 0.0 || floorPreview < ceilingPreview))
                {
                    floorRow = true;
                    ceilingRow = false;
                }
            }

            const SectorDef* sector = playerSector;
            double distance = -1.0;
            double worldX = m_posX;
            double worldY = m_posY;
            bool forceSky = false;
            bool portalTracedSurface = false;

            if (column.eventCount > 0)
            {
                // Trace the actual 3D floor/ceiling ray sector by sector. A
                // plane belongs to the current sector only until the ray
                // reaches it or passes through a valid vertical opening,
                // whichever happens first. Resolving only the eventual XY
                // point made sloped raster floors switch to the far sector and
                // redraw the rear part differently while crossing boundaries.
                const SectorDef* current = playerSector;
                double traversedDistance = 0.0;
                for (int traversal = 0; traversal < 48 && current; ++traversal)
                {
                    // An outdoor sector has no finite ceiling plane, but that
                    // does not mean the whole upper half of the column is sky.
                    // The ray may first pass through a doorway into a roofed
                    // room.  The old early return here is why the courtyard
                    // sky was painted across the rock stairwell even though
                    // its first tread and the room behind it have a ceiling.
                    const bool unboundedSky =
                        ceilingRow && current->skyCeiling;
                    const double surfaceDistance = unboundedSky
                        ? -1.0
                        : solveDistance(*current, floorRow, rayX, rayY, verticalStep);
                    const bool surfaceInCurrentSpan =
                        surfaceDistance > traversedDistance + 0.001;
                    const PortalClipEvent* nextPortal = nullptr;
                    char nextSymbol = '\0';
                    for (int eventIndex = 0; eventIndex < column.eventCount; ++eventIndex)
                    {
                        const PortalClipEvent& event = column.events[eventIndex];
                        if (event.distance <= traversedDistance + 0.002)
                            continue;
                        if (surfaceInCurrentSpan &&
                            event.distance >= surfaceDistance - 0.001)
                            break;
                        if (event.sectorA == current->symbol)
                            nextSymbol = event.sectorB;
                        else if (event.sectorB == current->symbol)
                            nextSymbol = event.sectorA;
                        else
                            continue;
                        nextPortal = &event;
                        break;
                    }

                    if (!nextPortal)
                    {
                        sector = current;
                        if (unboundedSky)
                        {
                            distance = std::max(0.02, traversedDistance);
                            forceSky = true;
                        }
                        else if (floorRow && !surfaceInCurrentSpan &&
                                 current->skyCeiling)
                        {
                            distance = std::max(0.02, traversedDistance);
                            forceSky = true;
                        }
                        else
                        {
                            distance = surfaceInCurrentSpan
                                ? surfaceDistance
                                : -1.0;
                        }
                        if (distance > 0.0)
                        {
                            worldX = m_posX + distance * rayX;
                            worldY = m_posY + distance * rayY;
                        }
                        portalTracedSurface = true;
                        break;
                    }

                    const double zAtPortal =
                        eyeZ + nextPortal->distance * verticalStep;
                    const bool belowPortalFloor =
                        zAtPortal < nextPortal->bottom - 0.01;
                    const bool abovePortalCeiling =
                        zAtPortal >= nextPortal->top - 0.01;
                    const bool blockedByPortal =
                        nextPortal->top <= nextPortal->bottom + 0.02 ||
                        (floorRow
                            ? ((!nextPortal->floorContinuous && belowPortalFloor) ||
                               abovePortalCeiling)
                            : (zAtPortal <= nextPortal->bottom + 0.01 ||
                               abovePortalCeiling));
                    if (blockedByPortal)
                    {
                        // The lintel/sill wall pass covers this ray at the
                        // boundary. Keep the current ceiling (or outdoor sky)
                        // as its background.
                        sector = current;
                        if (floorRow &&
                            (!surfaceInCurrentSpan ||
                             surfaceDistance > nextPortal->distance + 0.001))
                        {
                            distance = -1.0;
                            portalTracedSurface = true;
                            break;
                        }
                        if (unboundedSky)
                        {
                            distance = std::max(0.02, nextPortal->distance);
                            forceSky = true;
                        }
                        else
                        {
                            distance = surfaceDistance;
                        }
                        if (distance > 0.0)
                        {
                            worldX = m_posX + distance * rayX;
                            worldY = m_posY + distance * rayY;
                        }
                        portalTracedSurface = true;
                        break;
                    }

                    traversedDistance = nextPortal->distance;
                    current = m_sectorLookup[
                        static_cast<unsigned char>(nextSymbol)];
                }
            }

            // A view from the upper forecourt can cross the flat threshold,
            // the stone neck and the earthen part of the same ramp. Two
            // fixed-point passes stopped on the second sector but kept the
            // previous plane distance, making the slope look like a raised
            // horizontal strip. Five cheap plane solves are enough for every
            // current raster map and normally converge after one or two.
            constexpr int maxIterations = 5;
            for (int iteration = 0;
                 !forceSky && !portalTracedSurface && iteration < maxIterations;
                 ++iteration)
            {
                distance = solveDistance(*sector, floorRow, rayX, rayY, verticalStep);
                if (distance <= 0.0)
                    break;
                worldX = m_posX + distance * rayX;
                worldY = m_posY + distance * rayY;
                const SectorDef* resolved = &sectorAtWorld(worldX, worldY);
                if (resolved == sector)
                    break;
                sector = resolved;
            }

            // Generated stair treads intentionally overlap the room polygon
            // that contains them. The portal trace determines which openings
            // the ray crossed, but the final XY point still has to be owned by
            // the top-most polygon at that position. Otherwise the raised room
            // floor remains stretched across the stair opening and hides all
            // lower treads. Re-solve the plane after applying polygon priority;
            // a few iterations are sufficient because each correction moves
            // the sample monotonically along the same camera ray.
            if (hasVectorGeometry() && !forceSky && distance > 0.0)
            {
                for (int ownershipIteration = 0; ownershipIteration < 6;
                     ++ownershipIteration)
                {
                    const SectorDef* owner = &sectorAtWorld(worldX, worldY);
                    if (owner->symbol == sector->symbol)
                        break;
                    sector = owner;
                    distance =
                        solveDistance(*sector, floorRow, rayX, rayY, verticalStep);
                    if (distance <= 0.0)
                        break;
                    worldX = m_posX + distance * rayX;
                    worldY = m_posY + distance * rayY;
                }
            }
            if (distance <= 0.0)
                continue;

            // A ceiling/floor plane from another sector is visible only through
            // the actual vertical portal opening. Without this test a low room
            // ceiling could be projected through a gate passage and float over
            // the courtyard. If the plane is hidden, extend the current sector's
            // surface behind the portal; the wall/lintel pass covers the boundary.
            if (ceilingRow && sector != playerSector &&
                !forceSky && !portalTracedSurface)
            {
                const double candidateZ = ceilingHeightAt(*sector, worldX, worldY);
                bool visible = distance <= column.solidDistance + 0.001;
                if (visible)
                {
                    if (hasVectorGeometry())
                    {
                        char currentSymbol = playerSector->symbol;
                        for (int eventIndex = 0; eventIndex < column.eventCount; ++eventIndex)
                        {
                            const PortalClipEvent& event = column.events[eventIndex];
                            if (event.distance >= distance - 0.001)
                                break;
                            char nextSymbol = '\0';
                            if (event.sectorA == currentSymbol)
                                nextSymbol = event.sectorB;
                            else if (event.sectorB == currentSymbol)
                                nextSymbol = event.sectorA;
                            else
                                continue;
                            const double zAtPortal =
                                eyeZ + (candidateZ - eyeZ) *
                                    (event.distance / distance);
                            if (event.top <= event.bottom + 0.02 ||
                                zAtPortal <= event.bottom + 0.01 ||
                                zAtPortal >= event.top - 0.01)
                            {
                                visible = false;
                                break;
                            }
                            currentSymbol = nextSymbol;
                        }
                        if (visible && currentSymbol != sector->symbol)
                            visible = false;
                    }
                    else
                    {
                        for (int eventIndex = 0; eventIndex < column.eventCount; ++eventIndex)
                        {
                            const PortalClipEvent& event = column.events[eventIndex];
                            if (event.distance >= distance - 0.001) break;
                            const double zAtPortal =
                                eyeZ + (candidateZ - eyeZ) *
                                    (event.distance / distance);
                            if (event.top <= event.bottom + 0.02 ||
                                zAtPortal <= event.bottom + 0.01 ||
                                zAtPortal >= event.top - 0.01)
                            {
                                visible = false;
                                break;
                            }
                        }
                    }
                }
                if (!visible)
                {
                    sector = playerSector;
                    distance = solveDistance(*sector, false, rayX, rayY, verticalStep);
                    if (distance <= 0.0) continue;
                    worldX = m_posX + distance * rayX;
                    worldY = m_posY + distance * rayY;
                }
            }

            std::uint32_t color = 0;
            // The sampled sector owns the ceiling. Using the player's outdoor
            // sector here made sky leak into roofed rooms; using the player's
            // covered sector made its ceiling stretch through a portal over an
            // open courtyard.
            if (forceSky || (ceilingRow && sector->skyCeiling))
            {
                color = argb(92, 112, 135);
                if (skyTexture && skyTexture->w > 0 && skyTexture->h > 0 && !skyTexture->pixels.empty())
                {
                    double u = skyAngleU + (t - 0.5) * skyFovPart;
                    u -= std::floor(u);
                    const int texX = std::clamp(static_cast<int>(u * skyTexture->w), 0, skyTexture->w - 1);
                    const int texY = std::clamp(static_cast<int>(skyV * skyTexture->h), 0, skyTexture->h - 1);
                    color = skyTexture->pixels[static_cast<std::size_t>(texY) * skyTexture->w + texX];
                }
                if (skyHaze > 0.0)
                    color = alphaBlend(color, skyHazeColor);
            }
            else
            {
                const TextureKey textureKey = floorRow ? sector->floorTexture : sector->ceilingTexture;
                const std::uint32_t fallback = floorRow
                    ? argb(sector->floorColorR, sector->floorColorG, sector->floorColorB)
                    : argb(sector->ceilingColorR, sector->ceilingColorG, sector->ceilingColorB);
                const double shade = std::clamp(sector->ambient / (1.0 + distance * 0.055), 0.20, 1.15);
                const double surfaceZ = floorRow
                    ? floorHeightAt(*sector, worldX, worldY)
                    : ceilingHeightAt(*sector, worldX, worldY);
                color = applyWorldLighting(sampleTexture(textureKey, worldX, worldY, fallback),
                                           shade, worldX, worldY, surfaceZ);
            }

            const std::size_t row = static_cast<std::size_t>(y) * screenW + x;
            for (int dx = 0; dx < blockWidth; ++dx)
            {
                m_framebuffer[row + dx] = color;
                // Floors and ceilings are real geometry as well. Without
                // recording their depth, a distant wall rendered afterwards
                // can overwrite an elevated floor or a stair tread.
                if (!forceSky)
                    m_dynamicDepthBuffer[row + dx] = distance;
            }
        }
    }
}

void BuildInteriorEngine::renderPolygonSurfaces(
    int screenW, int screenH, int horizon, double eyeZ)
{
    const double dirX = std::cos(m_angle);
    const double dirY = std::sin(m_angle);
    const double planeScale = std::tan(m_fov * 0.5);
    const double planeX = -dirY * planeScale;
    const double planeY = dirX * planeScale;
    const double determinant = planeX * dirY - dirX * planeY;
    if (std::abs(determinant) <= 1.0e-10)
        return;
    const double invDet = 1.0 / determinant;

    // Sky is the infinitely distant background. Real polygon ceilings are
    // depth-tested over it, while door/window openings naturally leave it
    // visible without extending one room's ceiling into another sector.
    const TextureRef* skyTexture =
        m_textureLookup[static_cast<std::size_t>('K')];
    const double skyAngleU = (m_angle + kPi) / (2.0 * kPi);
    const double skyFovPart = m_fov / (2.0 * kPi);
    const int skyRows = std::clamp(horizon, 0, screenH);
    for (int y = 0; y < skyRows; ++y)
    {
        double skyV = std::clamp(
            static_cast<double>(y) / std::max(1, horizon), 0.0, 1.0);
        skyV = 0.02 + std::pow(skyV, 0.90) * 0.96;
        const double haze =
            std::clamp((skyV - 0.68) / 0.30, 0.0, 1.0) * 0.24;
        const std::uint32_t hazeColor =
            argb(150, 162, 170, static_cast<int>(haze * 255.0));
        const std::size_t row = static_cast<std::size_t>(y) * screenW;
        for (int x = 0; x < screenW; ++x)
        {
            std::uint32_t color = argb(92, 112, 135);
            if (skyTexture && skyTexture->w > 0 && skyTexture->h > 0 &&
                !skyTexture->pixels.empty())
            {
                const double t =
                    screenW > 1 ? static_cast<double>(x) / (screenW - 1) : 0.5;
                double u = skyAngleU + (t - 0.5) * skyFovPart;
                u -= std::floor(u);
                const int textureX = std::clamp(
                    static_cast<int>(u * skyTexture->w), 0, skyTexture->w - 1);
                const int textureY = std::clamp(
                    static_cast<int>(skyV * skyTexture->h),
                    0, skyTexture->h - 1);
                color = skyTexture->pixels[
                    static_cast<std::size_t>(textureY) * skyTexture->w +
                    textureX];
            }
            if (haze > 0.0)
                color = alphaBlend(color, hazeColor);
            m_framebuffer[row + static_cast<std::size_t>(x)] = color;
        }
    }

    struct ClipVertex
    {
        double cameraX = 0.0;
        double depth = 0.0;
        double worldX = 0.0;
        double worldY = 0.0;
        double worldZ = 0.0;
    };
    constexpr double nearPlane = 0.035;

    auto makeClipVertex = [&](double worldX, double worldY, double worldZ) {
        const double relX = worldX - m_posX;
        const double relY = worldY - m_posY;
        ClipVertex vertex;
        vertex.cameraX = invDet * (dirY * relX - dirX * relY);
        vertex.depth = invDet * (-planeY * relX + planeX * relY);
        vertex.worldX = worldX;
        vertex.worldY = worldY;
        vertex.worldZ = worldZ;
        return vertex;
    };
    auto interpolate = [](const ClipVertex& a, const ClipVertex& b, double t) {
        ClipVertex result;
        result.cameraX = a.cameraX + (b.cameraX - a.cameraX) * t;
        result.depth = a.depth + (b.depth - a.depth) * t;
        result.worldX = a.worldX + (b.worldX - a.worldX) * t;
        result.worldY = a.worldY + (b.worldY - a.worldY) * t;
        result.worldZ = a.worldZ + (b.worldZ - a.worldZ) * t;
        return result;
    };
    auto project = [&](const ClipVertex& source) {
        ProjectedVertex result;
        result.x = (screenW * 0.5) *
            (1.0 + source.cameraX / source.depth);
        result.y = static_cast<double>(
            projectZ(source.worldZ, source.depth, screenH, horizon, eyeZ));
        result.depth = source.depth;
        // Surface UV coordinates are world coordinates. The rasterizer repeats
        // them instead of clamping, preserving the existing material scale.
        result.u = source.worldX;
        result.v = source.worldY;
        return result;
    };

    m_surfaceOcclusionColumns.assign(
        static_cast<std::size_t>(screenW), SurfaceOcclusionColumn{});
    const int occlusionStep = m_fastFloorCasting ? 2 : 1;
    auto addSurfaceOcclusionHit = [](SurfaceOcclusionColumn& column,
                                     const SurfaceOcclusionHit& hit) {
        const int capacity = static_cast<int>(column.hits.size());
        if (column.count < capacity)
        {
            int index = column.count++;
            column.hits[static_cast<std::size_t>(index)] = hit;
            while (index > 0 &&
                   column.hits[static_cast<std::size_t>(index)].distance <
                   column.hits[static_cast<std::size_t>(index - 1)].distance)
            {
                std::swap(column.hits[static_cast<std::size_t>(index)],
                          column.hits[static_cast<std::size_t>(index - 1)]);
                --index;
            }
            return;
        }
        if (hit.distance >= column.hits.back().distance)
            return;
        column.hits.back() = hit;
        for (int index = capacity - 1;
             index > 0 &&
             column.hits[static_cast<std::size_t>(index)].distance <
             column.hits[static_cast<std::size_t>(index - 1)].distance;
             --index)
        {
            std::swap(column.hits[static_cast<std::size_t>(index)],
                      column.hits[static_cast<std::size_t>(index - 1)]);
        }
    };
    auto collectSurfaceOccluders = [&](const std::vector<WallSegmentDef>& walls,
                                       SurfaceOcclusionColumn& column,
                                       double rayX, double rayY) {
        for (const WallSegmentDef& wall : walls)
        {
            const double sx = wall.x1 - wall.x0;
            const double sy = wall.y1 - wall.y0;
            const double denominator = rayX * sy - rayY * sx;
            if (std::abs(denominator) < 1.0e-10)
                continue;
            const double qx = wall.x0 - m_posX;
            const double qy = wall.y0 - m_posY;
            const double distance = (qx * sy - qy * sx) / denominator;
            const double u = (qx * rayY - qy * rayX) / denominator;
            constexpr double kJointEpsilon = 0.0035;
            if (distance <= 0.02 ||
                u < -kJointEpsilon ||
                u > 1.0 + kJointEpsilon)
                continue;
            if (!wall.twoSided)
            {
                const double normalX = sy;
                const double normalY = -sx;
                if (normalX * rayX + normalY * rayY >= 0.0)
                    continue;
            }
            const double amount = std::clamp(u, 0.0, 1.0);
            addSurfaceOcclusionHit(column, {
                distance,
                wallBottomAt(wall, amount),
                wallTopAt(wall, amount)
            });
        }
    };
    for (int x = 0; x < screenW; x += occlusionStep)
    {
        const int blockWidth = std::min(occlusionStep, screenW - x);
        const double sampleX = x + (blockWidth - 1) * 0.5;
        const double cameraX =
            2.0 * sampleX / static_cast<double>(screenW) - 1.0;
        const double rayX = dirX + planeX * cameraX;
        const double rayY = dirY + planeY * cameraX;
        SurfaceOcclusionColumn column;
        collectSurfaceOccluders(m_wallSegments, column, rayX, rayY);
        collectSurfaceOccluders(m_polygonBoundaryWalls, column, rayX, rayY);
        for (int dx = 0; dx < blockWidth; ++dx)
            m_surfaceOcclusionColumns[static_cast<std::size_t>(x + dx)] =
                column;
    }

    for (const PolygonSectorRegion& region : m_polygonSectors)
    {
        const SectorDef* sector =
            m_sectorLookup[static_cast<unsigned char>(region.sector)];
        if (!sector || !region.outlineSimple || region.triangles.empty())
            continue;

        const int surfaceCount = region.hasSupportBottom ? 3 : 2;
        for (int surfaceKind = 0; surfaceKind < surfaceCount; ++surfaceKind)
        {
            const bool floorSurface = surfaceKind == 0;
            const bool ceilingSurface = surfaceKind == 1;
            const bool supportBottomSurface = surfaceKind == 2;
            if (ceilingSurface && sector->skyCeiling)
                continue;

            for (const std::array<int, 3>& triangle : region.triangles)
            {
                std::vector<ClipVertex> input;
                input.reserve(4);
                for (const int index : triangle)
                {
                    const auto& point =
                        region.vertices[static_cast<std::size_t>(index)];
                    const double z = floorSurface
                        ? floorHeightAt(*sector, point[0], point[1])
                        : (supportBottomSurface
                            ? region.supportBottomZ
                            : ceilingHeightAt(
                                *sector, point[0], point[1]));
                    input.push_back(makeClipVertex(point[0], point[1], z));
                }

                std::vector<ClipVertex> clipped;
                clipped.reserve(4);
                ClipVertex previous = input.back();
                bool previousInside = previous.depth >= nearPlane;
                for (const ClipVertex& current : input)
                {
                    const bool currentInside = current.depth >= nearPlane;
                    if (currentInside != previousInside)
                    {
                        const double denominator = current.depth - previous.depth;
                        if (std::abs(denominator) > 1.0e-12)
                        {
                            const double amount =
                                (nearPlane - previous.depth) / denominator;
                            clipped.push_back(
                                interpolate(previous, current, amount));
                        }
                    }
                    if (currentInside)
                        clipped.push_back(current);
                    previous = current;
                    previousInside = currentInside;
                }
                if (clipped.size() < 3)
                    continue;

                const ProjectedVertex first = project(clipped[0]);
                for (std::size_t vertex = 1;
                     vertex + 1 < clipped.size(); ++vertex)
                {
                    drawPolygonSurfaceTriangle(
                        first, project(clipped[vertex]),
                        project(clipped[vertex + 1]),
                        region, *sector, surfaceKind, eyeZ);
                }
            }
        }
    }
}

void BuildInteriorEngine::drawPolygonSurfaceTriangle(
    const ProjectedVertex& a, const ProjectedVertex& b,
    const ProjectedVertex& c, const PolygonSectorRegion& region,
    const SectorDef& sector, int surfaceKind, double eyeZ)
{
    const bool floorSurface = surfaceKind == 0;
    const bool supportBottomSurface = surfaceKind == 2;
    const double area =
        (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(area) < 1.0e-8)
        return;

    const int minX = std::max(
        0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int maxX = std::min(
        m_sceneW - 1,
        static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int minY = std::max(
        0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int maxY = std::min(
        m_sceneH - 1,
        static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    if (minX > maxX || minY > maxY)
        return;

    const double invA = 1.0 / a.depth;
    const double invB = 1.0 / b.depth;
    const double invC = 1.0 / c.depth;
    const TextureKey textureKey = floorSurface
        ? sector.floorTexture
        : (supportBottomSurface
            ? sector.boundaryTexture
            : sector.ceilingTexture);
    const std::uint32_t fallback = floorSurface
        ? argb(sector.floorColorR, sector.floorColorG, sector.floorColorB)
        : (supportBottomSurface
            ? argb(96, 88, 76)
            : argb(
                sector.ceilingColorR, sector.ceilingColorG,
                sector.ceilingColorB));
    const char playerSectorSymbol = sectorAtPlayer().symbol;

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            const double pixelX = x + 0.5;
            const double pixelY = y + 0.5;
            const double w0 =
                ((b.x - pixelX) * (c.y - pixelY) -
                 (b.y - pixelY) * (c.x - pixelX)) / area;
            const double w1 =
                ((c.x - pixelX) * (a.y - pixelY) -
                 (c.y - pixelY) * (a.x - pixelX)) / area;
            const double w2 = 1.0 - w0 - w1;
            if (w0 < -1.0e-5 || w1 < -1.0e-5 || w2 < -1.0e-5)
                continue;

            const double invDepth = w0 * invA + w1 * invB + w2 * invC;
            if (invDepth <= 1.0e-10)
                continue;
            const double depth = 1.0 / invDepth;
            const std::size_t pixel =
                static_cast<std::size_t>(y) * m_sceneW + x;
            if (depth >= m_dynamicDepthBuffer[pixel] - 1.0e-5)
                continue;

            const double worldX =
                (w0 * a.u * invA + w1 * b.u * invB + w2 * c.u * invC) /
                invDepth;
            const double worldY =
                (w0 * a.v * invA + w1 * b.v * invB + w2 * c.v * invC) /
                invDepth;

            // Only stairs that descend through an already elevated floor cut
            // the underlying room surface. Internal stairs keep the floor
            // below their masonry body, which closes the old black void. The
            // V8 fallback scanned all 72 polygons for many pixels on every
            // frame and caused visible stutter on the stairs.
            if (floorSurface)
            {
                const char cachedOwner =
                    sectorSymbolAtWorld(worldX, worldY);
                bool cutByLaterPolygon =
                    cachedOwner != region.sector &&
                    m_floorCutoutSectors[
                        static_cast<unsigned char>(cachedOwner)];

                // A 1/8 m lookup cell can straddle the diagonal edge of the
                // rock stair. Resolve only the handful of actual cutout
                // polygons, never the entire polygon collection.
                if (!cutByLaterPolygon &&
                    cachedOwner == region.sector &&
                    !region.cutsUnderlyingFloor &&
                    worldX >= m_floorCutoutMinX - 1.0e-6 &&
                    worldX <= m_floorCutoutMaxX + 1.0e-6 &&
                    worldY >= m_floorCutoutMinY - 1.0e-6 &&
                    worldY <= m_floorCutoutMaxY + 1.0e-6)
                {
                    for (const std::size_t cutoutIndex :
                         m_floorCutoutPolygonIndices)
                    {
                        const PolygonSectorRegion& cutout =
                            m_polygonSectors[cutoutIndex];
                        if (&cutout == &region ||
                            worldX < cutout.minX - 1.0e-6 ||
                            worldX > cutout.maxX + 1.0e-6 ||
                            worldY < cutout.minY - 1.0e-6 ||
                            worldY > cutout.maxY + 1.0e-6)
                            continue;
                        if (pointInPolygon(cutout, worldX, worldY))
                        {
                            cutByLaterPolygon = true;
                            break;
                        }
                    }
                }
                if (cutByLaterPolygon)
                    continue;
            }

            const double worldZ = floorSurface
                ? floorHeightAt(sector, worldX, worldY)
                : (supportBottomSurface
                    ? region.supportBottomZ
                    : ceilingHeightAt(sector, worldX, worldY));
            if ((floorSurface || supportBottomSurface) &&
                region.sector != playerSectorSymbol)
            {
                bool visible = true;
                if (x >= 0 &&
                    x < static_cast<int>(m_surfaceOcclusionColumns.size()))
                {
                    const SurfaceOcclusionColumn& column =
                        m_surfaceOcclusionColumns[static_cast<std::size_t>(x)];
                    for (int hitIndex = 0; hitIndex < column.count; ++hitIndex)
                    {
                        const SurfaceOcclusionHit& hit =
                            column.hits[static_cast<std::size_t>(hitIndex)];
                        if (hit.distance >= depth - 0.01)
                            break;
                        const double z = eyeZ +
                            (worldZ - eyeZ) * (hit.distance / depth);
                        if (z > hit.bottom + 0.01 &&
                            z < hit.top - 0.01)
                        {
                            visible = false;
                            break;
                        }
                    }
                }
                else
                {
                    visible =
                        surfaceVisibleAlongRay(worldX, worldY, worldZ, eyeZ);
                }
                if (!visible)
                    continue;
            }

            const double shade = std::clamp(
                sector.ambient / (1.0 + depth * 0.055), 0.20, 1.15);
            std::uint32_t color =
                sampleTexture(textureKey, worldX, worldY, fallback);
            if (((color >> 24u) & 0xffu) < 8u)
                continue;
            color = applyWorldLighting(
                color, shade, worldX, worldY, worldZ);
            m_framebuffer[pixel] = color;
            m_dynamicDepthBuffer[pixel] = depth;
        }
    }
}

void BuildInteriorEngine::renderDoorSoffits(
    int screenW, int screenH, int horizon, double eyeZ)
{
    if (!hasVectorGeometry())
        return;

    const double dirX = std::cos(m_angle);
    const double dirY = std::sin(m_angle);
    const double planeScale = std::tan(m_fov * 0.5);
    const double planeX = -dirY * planeScale;
    const double planeY = dirX * planeScale;
    const double determinant = planeX * dirY - dirX * planeY;
    if (std::abs(determinant) <= 1.0e-10)
        return;
    const double invDet = 1.0 / determinant;

    auto project = [&](double worldX, double worldY, double worldZ,
                       double u, double v, ProjectedVertex& out) {
        const double relX = worldX - m_posX;
        const double relY = worldY - m_posY;
        const double cameraX =
            invDet * (dirY * relX - dirX * relY);
        const double depth =
            invDet * (-planeY * relX + planeX * relY);
        if (depth <= 0.035)
            return false;
        out.x = (screenW * 0.5) * (1.0 + cameraX / depth);
        out.y = static_cast<double>(
            projectZ(worldZ, depth, screenH, horizon, eyeZ));
        out.depth = depth;
        out.u = u;
        out.v = v;
        return true;
    };

    auto drawWorldQuad = [&](double ax, double ay, double az,
                             double au, double av,
                             double bx, double by, double bz,
                             double bu, double bv,
                             double cx, double cy, double cz,
                             double cu, double cv,
                             double dx, double dy, double dz,
                             double du, double dv,
                             const TextureRef& texture,
                             double shade)
    {
        ProjectedVertex a, b, c, d;
        if (!project(ax, ay, az, au, av, a) ||
            !project(bx, by, bz, bu, bv, b) ||
            !project(cx, cy, cz, cu, cv, c) ||
            !project(dx, dy, dz, du, dv, d))
            return;
        drawTexturedQuad(a, b, c, d, texture, shade);
    };

    for (const DoorDef& door : m_doors)
    {
        if (!door.hasSegment)
            continue;

        const double segmentX = door.segmentX1 - door.segmentX0;
        const double segmentY = door.segmentY1 - door.segmentY0;
        const double segmentLength = std::hypot(segmentX, segmentY);
        if (segmentLength <= 0.05)
            continue;
        const double normalX = -segmentY / segmentLength;
        const double normalY = segmentX / segmentLength;
        const double middleX =
            (door.segmentX0 + door.segmentX1) * 0.5;
        const double middleY =
            (door.segmentY0 + door.segmentY1) * 0.5;

        double bottomZ = 0.0;
        double topZ = 0.0;
        doorVerticalBounds(door, bottomZ, topZ);
        double negativeDepth = 0.0;
        double positiveDepth = 0.0;
        char negativeSymbol = '\0';
        char positiveSymbol = '\0';
        doorRevealDepths(
            door, negativeDepth, positiveDepth,
            negativeSymbol, positiveSymbol);

        const SectorDef* negativeSector =
            negativeSymbol == '\0' ? nullptr :
            m_sectorLookup[static_cast<unsigned char>(negativeSymbol)];
        const SectorDef* positiveSector =
            positiveSymbol == '\0' ? nullptr :
            m_sectorLookup[static_cast<unsigned char>(positiveSymbol)];
        const SectorDef* coveredSector =
            negativeSector && !negativeSector->skyCeiling
                ? negativeSector
                : (positiveSector && !positiveSector->skyCeiling
                    ? positiveSector : nullptr);
        if (!coveredSector)
            continue;

        const TextureRef* ceilingTexture =
            m_textureLookup[
                static_cast<unsigned char>(
                    coveredSector->ceilingTexture)];
        const TextureRef* revealTexture =
            m_textureLookup[
                static_cast<unsigned char>(
                    coveredSector->boundaryTexture)];
        if (!ceilingTexture || ceilingTexture->pixels.empty() ||
            !revealTexture || revealTexture->pixels.empty())
            continue;

        const double ax = door.segmentX0 - normalX * negativeDepth;
        const double ay = door.segmentY0 - normalY * negativeDepth;
        const double bx = door.segmentX1 - normalX * negativeDepth;
        const double by = door.segmentY1 - normalY * negativeDepth;
        const double cx = door.segmentX1 + normalX * positiveDepth;
        const double cy = door.segmentY1 + normalY * positiveDepth;
        const double dx = door.segmentX0 + normalX * positiveDepth;
        const double dy = door.segmentY0 + normalY * positiveDepth;

        const double distance =
            std::hypot(middleX - m_posX, middleY - m_posY);
        const double shade = std::clamp(
            coveredSector->ambient / (1.0 + distance * 0.045),
            0.22, 1.10);

        // The complete top of the passage, from the visible door plane to the
        // actual polygon-room boundary.  This replaces V10's fixed 46 cm patch
        // that stopped in the middle of one-metre-thick walls.
        drawWorldQuad(
            ax, ay, topZ, 0.0, 1.0,
            bx, by, topZ, 1.0, 1.0,
            cx, cy, topZ, 1.0, 0.0,
            dx, dy, topZ, 0.0, 0.0,
            *ceilingTexture, shade);

        // Close both reveal sides.  Detached room polygons otherwise leave
        // open vertical slits beside an apparently complete soffit.
        drawWorldQuad(
            ax, ay, topZ, 0.0, 0.0,
            dx, dy, topZ, 1.0, 0.0,
            dx, dy, bottomZ, 1.0, 1.0,
            ax, ay, bottomZ, 0.0, 1.0,
            *revealTexture, shade * 0.86);
        drawWorldQuad(
            cx, cy, topZ, 0.0, 0.0,
            bx, by, topZ, 1.0, 0.0,
            bx, by, bottomZ, 1.0, 1.0,
            cx, cy, bottomZ, 0.0, 1.0,
            *revealTexture, shade * 0.78);

        auto wallTopAtPoint = [&](const SectorDef& sector,
                                  double worldX, double worldY)
        {
            const double floor =
                floorHeightAt(sector, worldX, worldY);
            const double ceiling =
                ceilingHeightAt(sector, worldX, worldY);
            return sector.wallHeight > 0.0
                ? std::min(ceiling, floor + sector.wallHeight)
                : ceiling;
        };

        auto renderLintelFace = [&](double x0, double y0,
                                    double x1, double y1,
                                    const SectorDef& sector,
                                    double faceShade)
        {
            const double faceX = (x0 + x1) * 0.5;
            const double faceY = (y0 + y1) * 0.5;
            const double faceTop =
                wallTopAtPoint(sector, faceX, faceY);
            if (faceTop <= topZ + 0.015)
                return;
            const TextureRef* wallTexture =
                m_textureLookup[
                    static_cast<unsigned char>(
                        sector.boundaryTexture)];
            if (!wallTexture || wallTexture->pixels.empty())
                return;
            drawWorldQuad(
                x0, y0, faceTop, 0.0, 0.0,
                x1, y1, faceTop, 1.0, 0.0,
                x1, y1, topZ, 1.0, 1.0,
                x0, y0, topZ, 0.0, 1.0,
                *wallTexture, faceShade);
        };

        // Explicit near and far lintel faces keep the area above the leaf
        // solid even when the two room polygons are separated by a masonry
        // band and therefore do not share a literal polygon edge.
        const SectorDef* doorPlaneSector =
            negativeSector && negativeSector->skyCeiling
                ? negativeSector
                : (positiveSector && positiveSector->skyCeiling
                    ? positiveSector : coveredSector);
        renderLintelFace(
            door.segmentX0, door.segmentY0,
            door.segmentX1, door.segmentY1,
            *doorPlaneSector, shade * 0.92);
        if (negativeDepth > 0.01 && negativeSector &&
            !negativeSector->skyCeiling)
            renderLintelFace(
                ax, ay, bx, by, *negativeSector, shade * 0.84);
        if (positiveDepth > 0.01 && positiveSector &&
            !positiveSector->skyCeiling)
            renderLintelFace(
                dx, dy, cx, cy, *positiveSector, shade * 0.84);
    }
}

std::uint32_t BuildInteriorEngine::sampleSky(int x, int y, int screenW, int screenH, int horizon) const
{
    (void)screenH;
    const std::uint32_t fallback = argb(92, 112, 135);
    const TextureRef* texture = m_textureLookup[static_cast<std::size_t>('K')];
    if (!texture || texture->w <= 0 || texture->h <= 0 || texture->pixels.empty())
        return fallback;

    // Cylindrical panorama: horizontal camera rotation scrolls the sky, while
    // looking up/down only changes the sampled vertical band. Unlike a tiled
    // ceiling this remains visually distant and does not "crawl" across the room.
    const double normalizedAngle = (m_angle + kPi) / (2.0 * kPi);
    const double horizontalFovPart = m_fov / (2.0 * kPi);
    const double screenU = screenW > 1 ? static_cast<double>(x) / (screenW - 1) : 0.5;
    double u = normalizedAngle + (screenU - 0.5) * horizontalFovPart;
    u -= std::floor(u);

    const int skyBottom = std::max(1, horizon);
    double v = static_cast<double>(y) / skyBottom;
    v = std::clamp(v, 0.0, 1.0);

    // Keep a little more cloud detail near the horizon and avoid sampling the
    // very top/bottom edge of the source texture.
    v = 0.02 + std::pow(v, 0.90) * 0.96;
    std::uint32_t color = sampleTexture('K', u, v, fallback);

    // A light atmospheric veil near the horizon hides hard texture edges and
    // makes the panorama feel distant instead of painted onto a ceiling.
    const double haze = std::clamp((v - 0.68) / 0.30, 0.0, 1.0) * 0.24;
    if (haze > 0.0)
    {
        const std::uint32_t hazeColor = argb(150, 162, 170, static_cast<int>(haze * 255.0));
        color = alphaBlend(color, hazeColor);
    }
    return color;
}

void BuildInteriorEngine::renderVectorWalls(int screenW, int screenH,
                                             int horizon, double eyeZ)
{
    const double dirX = std::cos(m_angle);
    const double dirY = std::sin(m_angle);
    const double planeScale = std::tan(m_fov * 0.5);
    const double planeX = -dirY * planeScale;
    const double planeY = dirX * planeScale;
    // Fast mode is useful in the game too, not only in the editor. Two output
    // columns share one ray, while the final SDL scaling still smooths the image.
    const int columnStep = m_fastFloorCasting ? 2 : 1;

    auto renderWallList = [&](const std::vector<WallSegmentDef>& walls,
                              int x, int width, double rayX, double rayY)
    {
        for (const WallSegmentDef& wall : walls)
        {
            const double sx = wall.x1 - wall.x0;
            const double sy = wall.y1 - wall.y0;
            const double denominator = rayX * sy - rayY * sx;
            if (std::abs(denominator) < 1.0e-10) continue;

            const double qx = wall.x0 - m_posX;
            const double qy = wall.y0 - m_posY;
            const double distance = (qx * sy - qy * sx) / denominator;
            const double u = (qx * rayY - qy * rayX) / denominator;
            constexpr double kJointEpsilon = 0.0035;
            if (distance <= 0.02 || u < -kJointEpsilon || u > 1.0 + kJointEpsilon)
                continue;
            if (!wall.twoSided)
            {
                const double normalX = sy;
                const double normalY = -sx;
                if (normalX * rayX + normalY * rayY >= 0.0) continue;
            }

            const double wallAmount = std::clamp(u, 0.0, 1.0);
            const double bottomZ = wallBottomAt(wall, wallAmount);
            const double topZ = wallTopAt(wall, wallAmount);
            if (topZ <= bottomZ + 0.015)
                continue;
            const int y0 = projectZ(topZ, distance, screenH, horizon, eyeZ);
            const int y1 = projectZ(bottomZ, distance, screenH, horizon, eyeZ);
            if (y1 < 0 || y0 >= screenH) continue;

            const double length = std::max(1.0e-6, std::hypot(sx, sy));
            const double textureU = wall.textureUOffset +
                std::clamp(u, 0.0, 1.0) * length * wall.textureScale;
            const double hitX = m_posX + distance * rayX;
            const double hitY = m_posY + distance * rayY;
            const double facingShade = 0.84 + 0.16 * std::abs(sy * rayX - sx * rayY) / length;
            const double textureVTop = wall.worldAlignedTexture
                ? topZ * wall.textureScale : 0.0;
            const double textureVBottom = wall.worldAlignedTexture
                ? bottomZ * wall.textureScale : 1.0;

            // drawTexturedColumn uses the per-pixel depth buffer, so wall order
            // does not need sorting. Removing the allocation + sort per column
            // is a large win on maps with many polygon edges and openings.
            drawTexturedColumn(x, y0, y1, wall.texture, textureU, distance,
                               wall.ambient * facingShade, 0, screenH - 1,
                               textureVTop, textureVBottom,
                               width, hitX, hitY,
                               (bottomZ + topZ) * 0.5);
        }
    };

    for (int x = 0; x < screenW; x += columnStep)
    {
        const int width = std::min(columnStep, screenW - x);
        const double sampleX = x + (width - 1) * 0.5;
        const double cameraX = 2.0 * sampleX / static_cast<double>(screenW) - 1.0;
        const double rayX = dirX + planeX * cameraX;
        const double rayY = dirY + planeY * cameraX;
        renderWallList(m_wallSegments, x, width, rayX, rayY);
        renderWallList(m_polygonBoundaryWalls, x, width, rayX, rayY);
    }
}

void BuildInteriorEngine::renderPortalWalls(int screenW, int screenH, int horizon, double eyeZ)
{
    if (hasVectorGeometry())
    {
        renderVectorWalls(screenW, screenH, horizon, eyeZ);
        return;
    }
    const double dirX = std::cos(m_angle);
    const double dirY = std::sin(m_angle);
    const double planeScale = std::tan(m_fov * 0.5);
    const double planeX = -dirY * planeScale;
    const double planeY = dirX * planeScale;

    // Fast mode casts raster wall columns in 2-pixel steps. This keeps the
    // exterior map responsive while the reduced internal buffer absorbs the
    // tiny horizontal loss in precision.
    const int columnStep = m_fastFloorCasting ? 2 : 1;
    for (int x = 0; x < screenW; x += columnStep)
    {
        const double sampleX = x + (std::min(columnStep, screenW - x) - 1) * 0.5;
        const double cameraX = 2.0 * sampleX / static_cast<double>(screenW) - 1.0;
        const double rayDirX = dirX + planeX * cameraX;
        const double rayDirY = dirY + planeY * cameraX;

        double arbitraryDistance = kHuge;
        double arbitraryU = 0.0;
        const WallSegmentDef* arbitraryWall = nearestWallSegmentHit(
            m_posX, m_posY, rayDirX, rayDirY, arbitraryDistance, arbitraryU);

        int mapX = static_cast<int>(std::floor(m_posX));
        int mapY = static_cast<int>(std::floor(m_posY));
        const double deltaX = std::abs(rayDirX) < 1e-12 ? kHuge : std::abs(1.0 / rayDirX);
        const double deltaY = std::abs(rayDirY) < 1e-12 ? kHuge : std::abs(1.0 / rayDirY);
        const int stepX = rayDirX < 0.0 ? -1 : 1;
        const int stepY = rayDirY < 0.0 ? -1 : 1;
        double sideX = rayDirX < 0.0 ? (m_posX - mapX) * deltaX : (mapX + 1.0 - m_posX) * deltaX;
        double sideY = rayDirY < 0.0 ? (m_posY - mapY) * deltaY : (mapY + 1.0 - m_posY) * deltaY;

        const SectorDef* currentSector = &sectorAt(mapX, mapY);
        int clipTop = 0;
        int clipBottom = screenH - 1;

        for (int step = 0; step < 96 && clipTop < clipBottom; ++step)
        {
            int side = 0;
            if (sideX < sideY)
            {
                sideX += deltaX;
                mapX += stepX;
                side = 0;
            }
            else
            {
                sideY += deltaY;
                mapY += stepY;
                side = 1;
            }

            double distance = side == 0
                ? (mapX - m_posX + (1.0 - stepX) * 0.5) / rayDirX
                : (mapY - m_posY + (1.0 - stepY) * 0.5) / rayDirY;
            if (distance < 0.02) distance = 0.02;

            if (arbitraryWall && arbitraryDistance < distance - 0.001)
            {
                const double hitX = m_posX + arbitraryDistance * rayDirX;
                const double hitY = m_posY + arbitraryDistance * rayDirY;
                const double wallX = arbitraryWall->x1 - arbitraryWall->x0;
                const double wallY = arbitraryWall->y1 - arbitraryWall->y0;
                const double wallLengthSq = wallX * wallX + wallY * wallY;
                const double amount = wallLengthSq > 1.0e-10
                    ? ((hitX - arbitraryWall->x0) * wallX +
                       (hitY - arbitraryWall->y0) * wallY) / wallLengthSq
                    : 0.0;
                const double bottomZ = wallBottomAt(*arbitraryWall, amount);
                const double topZ = wallTopAt(*arbitraryWall, amount);
                const int y0 = projectZ(topZ, arbitraryDistance, screenH, horizon, eyeZ);
                const int y1 = projectZ(bottomZ, arbitraryDistance, screenH, horizon, eyeZ);
                drawTexturedColumn(x, y0, y1, arbitraryWall->texture, arbitraryU,
                                   arbitraryDistance, arbitraryWall->ambient,
                                   clipTop, clipBottom, 0.0, 1.0, columnStep,
                                   hitX, hitY, (bottomZ + topZ) * 0.5);
                const bool partialHeightWall =
                    topZ - bottomZ <= 1.35 && topZ < eyeZ + 1.8;
                if (partialHeightWall)
                {
                    clipBottom = std::min(clipBottom, y0 - 1);
                    arbitraryWall = nullptr;
                    arbitraryDistance = kHuge;
                    if (clipTop >= clipBottom)
                        break;
                    continue;
                }
                const int coveredColumns = std::min(columnStep, screenW - x);
                for (int ox = 0; ox < coveredColumns; ++ox)
                    m_zBuffer[x + ox] = arbitraryDistance;
                break;
            }

            double wallX = side == 0 ? m_posY + distance * rayDirY : m_posX + distance * rayDirX;
            wallX -= std::floor(wallX);
            if ((side == 0 && rayDirX > 0.0) || (side == 1 && rayDirY < 0.0))
                wallX = 1.0 - wallX;

            const TextureKey cell = cellAt(mapX, mapY);
            bool hitSolid = !isInside(mapX, mapY) || (!isEmptyCell(cell) && cell != static_cast<TextureKey>('D'));
            TextureKey hitTexture = cell;
            if (hitSolid)
            {
                hitTexture =
                    (cell != kNoTexture && cell == m_defaultSolidTexture)
                        ? currentSector->boundaryTexture
                        : (m_textureLookup[static_cast<std::size_t>(cell)]
                            ? cell
                            : currentSector->boundaryTexture);
            }
            double hitU = wallX;
            const DoorDef* hitDoor = nullptr;

            if (cell == static_cast<TextureKey>('D'))
            {
                hitDoor = doorAt(mapX, mapY);
                if (!hitDoor)
                {
                    hitSolid = true;
                    hitTexture = static_cast<TextureKey>('D');
                }
                else
                {
                    hitTexture = hitDoor->texture;
                    hitU = doorTextureU(*hitDoor, mapX, mapY, wallX);
                    if (hitDoor->motion == DoorMotion::Swing)
                    {
                        // The rotating leaf is drawn later as real dynamic 3D
                        // geometry. Treat its grid cell as a portal here.
                        hitSolid = false;
                    }
                    else if (hitDoor->motion == DoorMotion::Slide)
                    {
                        const double shifted = hitU + hitDoor->openAmount;
                        if (shifted >= 1.0)
                            hitSolid = false;
                        else
                        {
                            hitSolid = true;
                            hitU = shifted;
                        }
                    }
                    else if (hitDoor->motion == DoorMotion::Raise)
                    {
                        hitSolid = hitDoor->openAmount < 0.995;
                    }
                    else
                    {
                        // A transition portal remains part of the wall. Pressing E
                        // changes the map immediately; there is no fake opening phase.
                        hitSolid = true;
                    }
                }
            }

            const double hitWorldX = m_posX + distance * rayDirX;
            const double hitWorldY = m_posY + distance * rayDirY;

            // A hinged door cell is a portal only below the top of the doorway.
            // Draw the masonry above it in the DDA pass so it participates in
            // the same column clipping as the surrounding wall. Rendering this
            // part later as a free quad caused the sky to leak above doors and
            // made the lintel appear too low or detached at oblique angles.
            if (!hitSolid && hitDoor && hitDoor->motion == DoorMotion::Swing)
            {
                const double currentFloor = floorHeightAt(*currentSector, hitWorldX, hitWorldY);
                const double currentCeiling = ceilingHeightAt(*currentSector, hitWorldX, hitWorldY);
                const double solidWallTop = currentSector->wallHeight > 0.0
                    ? currentFloor + currentSector->wallHeight
                    : currentCeiling;
                const double requestedHeight = hitDoor->height > 0.0
                    ? hitDoor->height
                    : (solidWallTop - currentFloor);
                const double doorTopZ = std::clamp(currentFloor + requestedHeight,
                                                   currentFloor + 0.35, solidWallTop);
                if (solidWallTop > doorTopZ + 0.001)
                {
                    const int upperTop = projectZ(solidWallTop, distance, screenH, horizon, eyeZ);
                    const int upperBottom = projectZ(doorTopZ, distance, screenH, horizon, eyeZ);
                    const double sideShade = side == 1 ? 0.78 : 1.0;
                    drawTexturedColumn(x, upperTop, upperBottom, currentSector->boundaryTexture,
                                       wallX, distance, currentSector->ambient * sideShade,
                                       clipTop, clipBottom, 0.0, 1.0, columnStep,
                                       hitWorldX, hitWorldY, (solidWallTop + doorTopZ) * 0.5);
                    clipTop = std::max(clipTop, upperBottom);
                }
            }

            if (hitSolid)
            {
                const double currentFloor = floorHeightAt(*currentSector, hitWorldX, hitWorldY);
                const double currentCeiling = ceilingHeightAt(*currentSector, hitWorldX, hitWorldY);
                const double solidWallTop = currentSector->wallHeight > 0.0
                    ? currentFloor + currentSector->wallHeight
                    : currentCeiling;
                const double sideShade = side == 1 ? 0.78 : 1.0;

                if (hitDoor && hitDoor->motion != DoorMotion::Slide)
                {
                    const double requestedHeight = hitDoor->height > 0.0
                        ? hitDoor->height
                        : (solidWallTop - currentFloor);
                    const double doorTopZ = std::clamp(currentFloor + requestedHeight,
                                                       currentFloor + 0.35, solidWallTop);

                    // Draw the masonry above the gate separately. This embeds the
                    // gate into the wall instead of showing it as a free billboard.
                    if (solidWallTop > doorTopZ + 0.001)
                    {
                        const int upperTop = projectZ(solidWallTop, distance, screenH, horizon, eyeZ);
                        const int upperBottom = projectZ(doorTopZ, distance, screenH, horizon, eyeZ);
                        drawTexturedColumn(x, upperTop, upperBottom, currentSector->boundaryTexture,
                                           wallX, distance, currentSector->ambient * sideShade,
                                           clipTop, clipBottom, 0.0, 1.0, columnStep,
                                           hitWorldX, hitWorldY, (solidWallTop + doorTopZ) * 0.5);
                    }

                    double visibleBottomZ = currentFloor;
                    if (hitDoor->motion == DoorMotion::Raise)
                        visibleBottomZ = currentFloor +
                            hitDoor->openAmount * (doorTopZ - currentFloor);

                    if (visibleBottomZ < doorTopZ - 0.001)
                    {
                        const int doorTop = projectZ(doorTopZ, distance, screenH, horizon, eyeZ);
                        const int doorBottom = projectZ(visibleBottomZ, distance, screenH, horizon, eyeZ);
                        drawTexturedColumn(x, doorTop, doorBottom, hitTexture, hitU, distance,
                                           currentSector->ambient * sideShade, clipTop, clipBottom, 0.0, 1.0, columnStep,
                                           hitWorldX, hitWorldY, (doorTopZ + visibleBottomZ) * 0.5);
                    }
                }
                else
                {
                    const int y0 = projectZ(solidWallTop, distance, screenH, horizon, eyeZ);
                    const int y1 = projectZ(currentFloor, distance, screenH, horizon, eyeZ);
                    drawTexturedColumn(x, y0, y1, hitTexture, hitU, distance,
                                       currentSector->ambient * sideShade, clipTop, clipBottom, 0.0, 1.0, columnStep,
                                       hitWorldX, hitWorldY, (solidWallTop + currentFloor) * 0.5);
                }

                const int coveredColumns = std::min(columnStep, screenW - x);
                for (int dx = 0; dx < coveredColumns; ++dx)
                    m_zBuffer[x + dx] = distance;
                break;
            }

            const SectorDef* nextSector = &sectorAt(mapX, mapY);
            if (nextSector->symbol != currentSector->symbol)
            {
                const double curCeilingZ = ceilingHeightAt(*currentSector, hitWorldX, hitWorldY);
                const double curFloorZ = floorHeightAt(*currentSector, hitWorldX, hitWorldY);
                const double nextCeilingZ = ceilingHeightAt(*nextSector, hitWorldX, hitWorldY);
                const double nextFloorZ = floorHeightAt(*nextSector, hitWorldX, hitWorldY);
                const int curFloor = projectZ(curFloorZ, distance, screenH, horizon, eyeZ);
                const int nextCeil = projectZ(nextCeilingZ, distance, screenH, horizon, eyeZ);
                const int nextFloor = projectZ(nextFloorZ, distance, screenH, horizon, eyeZ);
                const int portalBottom =
                    projectZ(std::max(curFloorZ, nextFloorZ), distance,
                             screenH, horizon, eyeZ);
                const bool floorIsContinuous =
                    std::abs(nextFloorZ - curFloorZ) <= kPortalFloorContinuityEpsilon;
                const bool ceilingIsContinuous =
                    std::abs(nextCeilingZ - curCeilingZ) <= kPortalCeilingContinuityEpsilon ||
                    (currentSector->skyCeiling && nextSector->skyCeiling);
                const double sideShade = side == 1 ? 0.78 : 1.0;
                const TextureKey boundary = nextSector->boundaryTexture;

                // A roofed passage entered from an outdoor sector needs a real
                // textured lintel above the opening. Use the outdoor sector's
                // authored wall height instead of its very high sky plane, so
                // the result is a believable gatehouse block rather than a
                // twelve-metre slab stretching into the sky.
                double portalTopZ = curCeilingZ;
                if (currentSector->skyCeiling && currentSector->wallHeight > 0.0)
                    portalTopZ = curFloorZ + currentSector->wallHeight;

                if (!ceilingIsContinuous &&
                    !nextSector->skyCeiling &&
                    nextCeilingZ < portalTopZ - 0.001)
                {
                    const int portalTop = projectZ(portalTopZ, distance, screenH, horizon, eyeZ);
                    drawTexturedColumn(x, portalTop, nextCeil, boundary, wallX, distance,
                                       nextSector->ambient * sideShade, clipTop, clipBottom, 0.0, 1.0, columnStep,
                                       hitWorldX, hitWorldY, (portalTopZ + nextCeilingZ) * 0.5);
                }
                if (!floorIsContinuous)
                {
                    drawTexturedColumn(x, nextFloor, curFloor, boundary, wallX, distance,
                                       nextSector->ambient * sideShade, clipTop, clipBottom, 0.0, 1.0, columnStep,
                                       hitWorldX, hitWorldY, (nextFloorZ + curFloorZ) * 0.5);
                }

                if (!ceilingIsContinuous)
                    clipTop = std::max(clipTop, nextCeil);
                if (!floorIsContinuous)
                    clipBottom = std::min(clipBottom, portalBottom);
                currentSector = nextSector;
            }
        }
    }
}

void BuildInteriorEngine::renderSwingDoors(int screenW, int screenH, int horizon, double eyeZ)
{
    const double dirX = std::cos(m_angle);
    const double dirY = std::sin(m_angle);
    const double planeScale = std::tan(m_fov * 0.5);
    const double planeX = -dirY * planeScale;
    const double planeY = dirX * planeScale;
    const double invDet = 1.0 / (planeX * dirY - dirX * planeY);

    auto project = [&](double wx, double wy, double wz, double u, double v,
                       ProjectedVertex& out) -> bool
    {
        const double relX = wx - m_posX;
        const double relY = wy - m_posY;
        const double transformX = invDet * (dirY * relX - dirX * relY);
        const double transformY = invDet * (-planeY * relX + planeX * relY);
        if (transformY <= 0.035)
            return false;
        out.x = (screenW * 0.5) * (1.0 + transformX / transformY);
        out.y = static_cast<double>(projectZ(wz, transformY, screenH, horizon, eyeZ));
        out.depth = transformY;
        out.u = u;
        out.v = v;
        return true;
    };

    auto texturedWorldQuad = [&](double ax, double ay, double az, double au, double av,
                                 double bx, double by, double bz, double bu, double bv,
                                 double cx, double cy, double cz, double cu, double cv,
                                 double dx, double dy, double dz, double du, double dv,
                                 const TextureRef& texture, double shade)
    {
        ProjectedVertex a, b, c, d;
        if (!project(ax, ay, az, au, av, a) || !project(bx, by, bz, bu, bv, b) ||
            !project(cx, cy, cz, cu, cv, c) || !project(dx, dy, dz, du, dv, d))
            return;
        drawTexturedQuad(a, b, c, d, texture, shade);
    };

    auto flatWorldQuad = [&](double ax, double ay, double az,
                             double bx, double by, double bz,
                             double cx, double cy, double cz,
                             double dx, double dy, double dz,
                             std::uint32_t color)
    {
        ProjectedVertex a, b, c, d;
        if (!project(ax, ay, az, 0.0, 0.0, a) || !project(bx, by, bz, 0.0, 0.0, b) ||
            !project(cx, cy, cz, 0.0, 0.0, c) || !project(dx, dy, dz, 0.0, 0.0, d))
            return;
        drawFlatQuad(a, b, c, d, color);
    };

    for (const DoorDef& door : m_doors)
    {
        // Legacy slide/raise leaves are still rendered by the grid DDA. Exact
        // polygon doors have no owning tile, so all their motions are drawn as
        // dynamic world-space geometry here.
        if (!door.hasSegment &&
            door.motion != DoorMotion::Swing &&
            door.motion != DoorMotion::Transition)
            continue;
        const TextureRef* texture = m_textureLookup[static_cast<std::size_t>(door.texture)];
        if (!texture || texture->pixels.empty())
            continue;

        double closedX0 = 0.0, closedY0 = 0.0, closedX1 = 0.0, closedY1 = 0.0;
        const char leafAxis = doorLeafAxis(door);
        if (door.hasSegment)
        {
            closedX0 = door.segmentX0;
            closedY0 = door.segmentY0;
            closedX1 = door.segmentX1;
            closedY1 = door.segmentY1;
        }
        else if (leafAxis == 'y')
        {
            closedX0 = door.x + 0.5;
            closedY0 = static_cast<double>(door.y);
            closedX1 = door.x + 0.5;
            closedY1 = door.y + static_cast<double>(door.span);
        }
        else
        {
            closedX0 = static_cast<double>(door.x);
            closedY0 = door.y + 0.5;
            closedX1 = door.x + static_cast<double>(door.span);
            closedY1 = door.y + 0.5;
        }

        double hingeX = 0.0, hingeY = 0.0, endX = 0.0, endY = 0.0;
        doorLeafSegment(door, hingeX, hingeY, endX, endY);
        const double centerX = (hingeX + endX) * 0.5;
        const double centerY = (hingeY + endY) * 0.5;
        const double doorwayX = (closedX0 + closedX1) * 0.5;
        const double doorwayY = (closedY0 + closedY1) * 0.5;
        const SectorDef& sector = sectorAtWorld(doorwayX, doorwayY);
        double floorZ = 0.0;
        double topZ = 0.0;
        doorVerticalBounds(door, floorZ, topZ);
        const double visibleBottomZ = door.motion == DoorMotion::Raise
            ? floorZ + door.openAmount * (topZ - floorZ)
            : floorZ;
        if (visibleBottomZ >= topZ - 0.002 ||
            std::hypot(endX - hingeX, endY - hingeY) <= 0.002)
            continue;
        const double ambient = std::clamp(sector.ambient /
            (1.0 + std::hypot(centerX - m_posX, centerY - m_posY) * 0.045), 0.25, 1.15);

        // The masonry above a hinged doorway is rendered during the main DDA
        // wall pass. Only the reveal surfaces are dynamic here; this avoids
        // equal-depth fighting and keeps the upper wall at its authored height.
        const TextureRef* revealTexture = m_textureLookup[static_cast<std::size_t>(sector.boundaryTexture)];

        // Give the doorway a real reveal: a textured horizontal soffit and
        // two jamb surfaces across the full wall cell. This prevents the sky
        // or a remote sector ceiling from leaking into the top of an open door.
        if (revealTexture && !door.hasSegment && door.motion == DoorMotion::Swing)
        {
            const double inset0 = 0.04;
            const double inset1 = 0.96;
            if (leafAxis == 'x')
            {
                const double xa = static_cast<double>(door.x);
                const double xb = door.x + static_cast<double>(door.span);
                const double ya = door.y + inset0;
                const double yb = door.y + inset1;
                // Keep only the vertical jambs here. The wall above the door is
                // already rendered in the main portal-wall pass; drawing an
                // additional horizontal soffit across the whole cell made the
                // lintel appear visually too low from the courtyard.
                texturedWorldQuad(xa, yb, topZ, 0.0, 0.0,
                                  xa, ya, topZ, 1.0, 0.0,
                                  xa, ya, floorZ, 1.0, 1.0,
                                  xa, yb, floorZ, 0.0, 1.0,
                                  *revealTexture, ambient * 0.82);
                texturedWorldQuad(xb, ya, topZ, 0.0, 0.0,
                                  xb, yb, topZ, 1.0, 0.0,
                                  xb, yb, floorZ, 1.0, 1.0,
                                  xb, ya, floorZ, 0.0, 1.0,
                                  *revealTexture, ambient * 0.72);
            }
            else
            {
                const double ya = static_cast<double>(door.y);
                const double yb = door.y + static_cast<double>(door.span);
                const double xa = door.x + inset0;
                const double xb = door.x + inset1;
                texturedWorldQuad(xb, ya, topZ, 0.0, 0.0,
                                  xa, ya, topZ, 1.0, 0.0,
                                  xa, ya, floorZ, 1.0, 1.0,
                                  xb, ya, floorZ, 0.0, 1.0,
                                  *revealTexture, ambient * 0.82);
                texturedWorldQuad(xa, yb, topZ, 0.0, 0.0,
                                  xb, yb, topZ, 1.0, 0.0,
                                  xb, yb, floorZ, 1.0, 1.0,
                                  xa, yb, floorZ, 0.0, 1.0,
                                  *revealTexture, ambient * 0.72);
            }
        }

        const double vx = endX - hingeX;
        const double vy = endY - hingeY;
        const double length = std::max(1.0e-6, std::hypot(vx, vy));
        const double nx = -vy / length * door.thickness * 0.5;
        const double ny = vx / length * door.thickness * 0.5;

        const double frontShade = ambient;
        const double backShade = ambient * 0.84;
        const double hingeU = door.hingeAtEnd ? door.textureU1 : door.textureU0;
        const double endU = door.hingeAtEnd ? door.textureU0 : door.textureU1;
        texturedWorldQuad(hingeX + nx, hingeY + ny, topZ, hingeU, 0.0,
                          endX + nx, endY + ny, topZ, endU, 0.0,
                          endX + nx, endY + ny, visibleBottomZ, endU, 1.0,
                          hingeX + nx, hingeY + ny, visibleBottomZ, hingeU, 1.0,
                          *texture, frontShade);
        texturedWorldQuad(endX - nx, endY - ny, topZ, endU, 0.0,
                          hingeX - nx, hingeY - ny, topZ, hingeU, 0.0,
                          hingeX - nx, hingeY - ny, visibleBottomZ, hingeU, 1.0,
                          endX - nx, endY - ny, visibleBottomZ, endU, 1.0,
                          *texture, backShade);

        const std::uint32_t edgeColor = modulate(sampleTexture(door.texture, 0.98, 0.5,
            argb(88, 57, 31)), ambient * 0.62);
        flatWorldQuad(endX + nx, endY + ny, topZ,
                      endX - nx, endY - ny, topZ,
                      endX - nx, endY - ny, visibleBottomZ,
                      endX + nx, endY + ny, visibleBottomZ,
                      edgeColor);
        flatWorldQuad(hingeX - nx, hingeY - ny, topZ,
                      hingeX + nx, hingeY + ny, topZ,
                      hingeX + nx, hingeY + ny, visibleBottomZ,
                      hingeX - nx, hingeY - ny, visibleBottomZ,
                      modulate(edgeColor, 0.82));
        flatWorldQuad(hingeX - nx, hingeY - ny, topZ,
                      endX - nx, endY - ny, topZ,
                      endX + nx, endY + ny, topZ,
                      hingeX + nx, hingeY + ny, topZ,
                      modulate(edgeColor, 1.08));
    }
}

void BuildInteriorEngine::renderBillboardSprite(const SpriteDef& sprite, int screenW, int screenH,
                                                 int horizon, double eyeZ)
{
    const TextureRef* texturePtr = m_textureLookup[static_cast<std::size_t>(sprite.texture)];
    if (!texturePtr || texturePtr->pixels.empty() || texturePtr->w <= 0 || texturePtr->h <= 0)
        return;
    const TextureRef& texture = *texturePtr;
    const TextureFrame* animationFrame = activeAnimationFrame(texture, sprite);
    const int textureW = animationFrame ? animationFrame->w : texture.w;
    const int textureH = animationFrame ? animationFrame->h : texture.h;
    const std::vector<std::uint32_t>& texturePixels = animationFrame
        ? animationFrame->pixels : texture.pixels;
    if (textureW <= 0 || textureH <= 0 || texturePixels.empty())
        return;

    const double dirX = std::cos(m_angle);
    const double dirY = std::sin(m_angle);
    const double planeScale = std::tan(m_fov * 0.5);
    const double planeX = -dirY * planeScale;
    const double planeY = dirX * planeScale;
    const double invDet = 1.0 / (planeX * dirY - dirX * planeY);
    const double relX = sprite.x - m_posX;
    const double relY = sprite.y - m_posY;
    const double transformX = invDet * (dirY * relX - dirX * relY);
    const double transformY = invDet * (-planeY * relX + planeX * relY);
    if (transformY <= 0.05) return;

    const int screenX = static_cast<int>((screenW / 2.0) * (1.0 + transformX / transformY));
    const SectorDef& sector = sectorAtWorld(sprite.x, sprite.y);
    const double bottomZ = floorHeightAt(sector, sprite.x, sprite.y) + sprite.zOffset;
    const double topZ = bottomZ + sprite.scale;
    int drawStartY = projectZ(topZ, transformY, screenH, horizon, eyeZ);
    int drawEndY = projectZ(bottomZ, transformY, screenH, horizon, eyeZ);
    if (drawEndY <= drawStartY) std::swap(drawStartY, drawEndY);
    const int spriteHeight = std::max(1, drawEndY - drawStartY);
    const double aspect = static_cast<double>(textureW) / textureH;
    const int spriteWidth = std::max(1, static_cast<int>(spriteHeight * aspect));
    const int drawStartX = screenX - spriteWidth / 2;
    const int drawEndX = screenX + spriteWidth / 2;
    const double shade = std::clamp(sector.ambient / (1.0 + transformY * 0.045), 0.25, 1.15);

    for (int stripe = std::max(0, drawStartX); stripe < std::min(screenW, drawEndX); ++stripe)
    {
        if (transformY >= m_zBuffer[stripe]) continue;
        const double u = static_cast<double>(stripe - drawStartX) / std::max(1, spriteWidth);
        const int texX = std::clamp(static_cast<int>(u * textureW), 0, textureW - 1);
        for (int y = std::max(0, drawStartY); y < std::min(screenH, drawEndY); ++y)
        {
            const std::size_t pixelIndex = static_cast<std::size_t>(y) * screenW + stripe;
            if (transformY >= m_dynamicDepthBuffer[pixelIndex]) continue;
            const double v = static_cast<double>(y - drawStartY) / std::max(1, spriteHeight);
            const int texY = std::clamp(static_cast<int>(v * textureH), 0, textureH - 1);
            std::uint32_t color = texturePixels[static_cast<std::size_t>(texY) * textureW + texX];
            if (((color >> 24u) & 0xffu) < 8u) continue;
            const double objectShade = sprite.emitsLight ? std::max(1.0, shade) : shade;
            color = applyWorldLighting(color, objectShade, sprite.x, sprite.y,
                                       bottomZ + sprite.scale * 0.56, &sprite);
            m_framebuffer[pixelIndex] = alphaBlend(m_framebuffer[pixelIndex], color);
            m_dynamicDepthBuffer[pixelIndex] = transformY;
        }
    }
}

void BuildInteriorEngine::renderVoxelSprite(const SpriteDef& sprite, int screenW, int screenH,
                                             int horizon, double eyeZ)
{
    const TextureRef* texture = m_textureLookup[static_cast<std::size_t>(sprite.texture)];
    if (!texture)
    {
        renderBillboardSprite(sprite, screenW, screenH, horizon, eyeZ);
        return;
    }
    const TextureFrame* animationFrame = activeAnimationFrame(*texture, sprite);
    const int textureW = animationFrame ? animationFrame->w : texture->w;
    const int textureH = animationFrame ? animationFrame->h : texture->h;
    const std::vector<VoxelCell>& voxelCells = animationFrame
        ? animationFrame->voxelCells : texture->voxelCells;
    if (voxelCells.empty() || textureH <= 0)
    {
        renderBillboardSprite(sprite, screenW, screenH, horizon, eyeZ);
        return;
    }

    const double dirX = std::cos(m_angle);
    const double dirY = std::sin(m_angle);
    const double planeScale = std::tan(m_fov * 0.5);
    const double planeX = -dirY * planeScale;
    const double planeY = dirX * planeScale;
    const double invDet = 1.0 / (planeX * dirY - dirX * planeY);
    const double ca = std::cos(sprite.yaw);
    const double sa = std::sin(sprite.yaw);
    const double aspect = static_cast<double>(textureW) / textureH;
    const double width = sprite.scale * aspect;
    const double depth = sprite.voxelDepth > 0.0
        ? sprite.voxelDepth
        : std::max(0.14, sprite.scale * 0.32);
    const double halfDepth = depth * 0.5;
    const SectorDef& sector = sectorAtWorld(sprite.x, sprite.y);
    const double baseZ = floorHeightAt(sector, sprite.x, sprite.y) + sprite.zOffset;
    const double objectDistance = std::hypot(sprite.x - m_posX, sprite.y - m_posY);
    const double baseShade = std::clamp(sector.ambient / (1.0 + objectDistance * 0.045), 0.25, 1.15);

    auto world = [&](double lx, double ly, double lz, double& wx, double& wy, double& wz)
    {
        wx = sprite.x + ca * lx - sa * ly;
        wy = sprite.y + sa * lx + ca * ly;
        wz = baseZ + lz * sprite.scale;
    };
    auto project = [&](double lx, double ly, double lz, ProjectedVertex& out) -> bool
    {
        double wx = 0.0, wy = 0.0, wz = 0.0;
        world(lx, ly, lz, wx, wy, wz);
        const double relX = wx - m_posX;
        const double relY = wy - m_posY;
        const double transformX = invDet * (dirY * relX - dirX * relY);
        const double transformY = invDet * (-planeY * relX + planeX * relY);
        if (transformY <= 0.035) return false;
        out.x = (screenW * 0.5) * (1.0 + transformX / transformY);
        out.y = static_cast<double>(projectZ(wz, transformY, screenH, horizon, eyeZ));
        out.depth = transformY;
        return true;
    };

    auto drawFace = [&](double x0, double y0, double z0,
                        double x1, double y1, double z1,
                        double x2, double y2, double z2,
                        double x3, double y3, double z3,
                        double normalX, double normalY, std::uint32_t color, double shade)
    {
        const double centerLX = (x0 + x1 + x2 + x3) * 0.25;
        const double centerLY = (y0 + y1 + y2 + y3) * 0.25;
        double centerWX = 0.0, centerWY = 0.0, centerWZ = 0.0;
        world(centerLX, centerLY, (z0 + z1 + z2 + z3) * 0.25, centerWX, centerWY, centerWZ);
        const double worldNX = ca * normalX - sa * normalY;
        const double worldNY = sa * normalX + ca * normalY;
        if (worldNX * (m_posX - centerWX) + worldNY * (m_posY - centerWY) <= -0.001)
            return;
        ProjectedVertex a, b, c, d;
        if (!project(x0, y0, z0, a) || !project(x1, y1, z1, b) ||
            !project(x2, y2, z2, c) || !project(x3, y3, z3, d))
            return;
        const double objectShade = sprite.emitsLight ? std::max(0.96, shade) : shade;
        drawFlatQuad(a, b, c, d, applyWorldLighting(color, objectShade,
                                                     centerWX, centerWY, centerWZ, &sprite));
    };

    for (const VoxelCell& cell : voxelCells)
    {
        const double x0 = cell.x0 * width;
        const double x1 = cell.x1 * width;
        const double z0 = cell.z0;
        const double z1 = cell.z1;
        drawFace(x0, -halfDepth, z1, x1, -halfDepth, z1,
                 x1, -halfDepth, z0, x0, -halfDepth, z0,
                 0.0, -1.0, cell.color, baseShade);
        drawFace(x1, halfDepth, z1, x0, halfDepth, z1,
                 x0, halfDepth, z0, x1, halfDepth, z0,
                 0.0, 1.0, cell.color, baseShade * 0.84);
        if (cell.exposed & kVoxelLeft)
            drawFace(x0, halfDepth, z1, x0, -halfDepth, z1,
                     x0, -halfDepth, z0, x0, halfDepth, z0,
                     -1.0, 0.0, cell.color, baseShade * 0.72);
        if (cell.exposed & kVoxelRight)
            drawFace(x1, -halfDepth, z1, x1, halfDepth, z1,
                     x1, halfDepth, z0, x1, -halfDepth, z0,
                     1.0, 0.0, cell.color, baseShade * 0.88);
        if (cell.exposed & kVoxelTop)
            drawFace(x0, -halfDepth, z1, x0, halfDepth, z1,
                     x1, halfDepth, z1, x1, -halfDepth, z1,
                     0.0, 0.0, cell.color, baseShade * 1.10);
        if (cell.exposed & kVoxelBottom)
            drawFace(x0, halfDepth, z0, x0, -halfDepth, z0,
                     x1, -halfDepth, z0, x1, halfDepth, z0,
                     0.0, 0.0, cell.color, baseShade * 0.58);
    }
}

void BuildInteriorEngine::renderSprites(int screenW, int screenH, int horizon, double eyeZ)
{
    if (m_sprites.empty()) return;

    std::vector<int> order(m_sprites.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const double dax = m_sprites[a].x - m_posX;
        const double day = m_sprites[a].y - m_posY;
        const double dbx = m_sprites[b].x - m_posX;
        const double dby = m_sprites[b].y - m_posY;
        return dax*dax + day*day > dbx*dbx + dby*dby;
    });

    for (int index : order)
    {
        const SpriteDef& sprite = m_sprites[index];
        if (sprite.renderMode == ObjectRenderMode::Voxel)
            renderVoxelSprite(sprite, screenW, screenH, horizon, eyeZ);
        else
            renderBillboardSprite(sprite, screenW, screenH, horizon, eyeZ);
    }
}

void BuildInteriorEngine::renderOverlay()
{
    if (!m_runtimeOverlayVisible)
        return;

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.58f);
    if (ImGui::Begin("##BuildInteriorOverlay", nullptr, flags))
    {
        ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.46f, 1.0f), U8("2.5D interiér – voxely a portálové sektory"));
        ImGui::Text("%s", m_displayName.c_str());
        ImGui::Text("Build: %s", kPolygonBuildStamp);
        ImGui::Text(U8("Kořen projektu: %s"),
                    ProjectRootPath().lexically_normal().string().c_str());
        ImGui::Text(U8("Runtime mapa: %s"), m_loadedPath.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted(U8("WASD pohyb, Shift běh, mezerník skok, E použít"));
        ImGui::TextUnformatted(U8("Myš: rozhlížení, kliknutí znovu zachytí kurzor"));
        ImGui::TextUnformatted(U8("ESC: herní menu"));
        ImGui::Text(U8("FPS %.0f | interní rozlišení %d x %d"), m_smoothedFps, m_sceneW, m_sceneH);
        ImGui::Separator();

        DoorDef* door = nearestUsableDoor(1.3);
        SpriteDef* sprite = nearestUsableSprite(1.3);
        if (sprite)
        {
            const std::string label = !sprite->interactionLabel.empty()
                ? sprite->interactionLabel
                : U8("E – vstoupit");
            ImGui::TextColored(ImVec4(0.80f, 0.95f, 0.62f, 1.0f), "%s", label.c_str());
        }
        else if (door)
        {
            const char* label = door->locked ? U8("Dveře jsou zamčené")
                : (door->motion == DoorMotion::Transition || !door->targetInterior.empty()) ? U8("E – projít branou")
                : door->targetOpen ? U8("E – zavřít dveře") : U8("E – otevřít dveře");
            ImGui::TextColored(ImVec4(0.80f, 0.95f, 0.62f, 1.0f), "%s", label);
        }

        const SectorDef& sector = sectorAtPlayer();
        const std::string sectorLabel = sectorDisplayLabel(sector.symbol);
        ImGui::Text(U8("Sektor %s: %s"), sectorLabel.c_str(), sector.name.c_str());
        ImGui::Text(U8("Výška %.2f až %.2f"),
                    floorHeightAtWorld(m_posX, m_posY),
                    ceilingHeightAtWorld(m_posX, m_posY));
        if (std::abs(sector.floorSlopeX) > 0.0001 || std::abs(sector.floorSlopeY) > 0.0001)
            ImGui::Text(U8("Sklon podlahy X %.2f / Y %.2f"), sector.floorSlopeX, sector.floorSlopeY);
        if (!m_status.empty())
            ImGui::TextColored(ImVec4(0.66f, 0.92f, 0.58f, 1.0f), "%s", m_status.c_str());
        if (!m_lastError.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.35f, 1.0f), "%s", m_lastError.c_str());
    }
    ImGui::End();
}

void BuildInteriorEngine::refreshEditorMapList()
{
    std::vector<EditorMapChoice> choices;
    const fs::path root = ProjectRootPath();

    auto readDisplayName = [](const fs::path& path, const std::string& fallback) {
        try
        {
            std::ifstream in(path);
            json source;
            if (in && (in >> source))
                return source.value("display_name", source.value("name", fallback));
        }
        catch (...) {}
        return fallback;
    };

    // Data-driven castle maps. They are loaded through the castle pipeline so
    // the source *.map.json remains the single source of truth.
    const fs::path castlesRoot = root / "data" / "castles";
    std::error_code ec;
    if (fs::exists(castlesRoot, ec))
    {
        for (fs::recursive_directory_iterator it(castlesRoot, ec), end; it != end && !ec; it.increment(ec))
        {
            if (!it->is_regular_file(ec))
                continue;
            const fs::path path = it->path();
            const std::string fileName = path.filename().string();
            constexpr const char* suffix = ".map.json";
            if (fileName.size() <= std::char_traits<char>::length(suffix) ||
                fileName.compare(fileName.size() - std::char_traits<char>::length(suffix),
                                 std::char_traits<char>::length(suffix), suffix) != 0)
                continue;
            if (path.parent_path().filename() != "maps")
                continue;

            const std::string castleId = path.parent_path().parent_path().filename().string();
            std::string mapId = fileName;
            mapId.resize(mapId.size() - std::char_traits<char>::length(suffix));
            const std::string displayName = readDisplayName(path, mapId);
            choices.push_back({castleId + " / " + displayName,
                               "castle:" + castleId + "/" + mapId});
        }
    }

    // Legacy JSON interiors remain useful for prototypes and test chambers.
    const fs::path interiorsRoot = root / "data" / "interiors";
    ec.clear();
    if (fs::exists(interiorsRoot, ec))
    {
        for (fs::recursive_directory_iterator it(interiorsRoot, ec), end; it != end && !ec; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || it->path().extension() != ".json")
                continue;
            const fs::path path = it->path();
            const std::string fallback = path.stem().string();
            const std::string displayName = readDisplayName(path, fallback);
            const fs::path relative = fs::relative(path, root, ec);
            const std::string target = ec ? path.string() : relative.generic_string();
            choices.push_back({"Interiéry / " + displayName, target});
            ec.clear();
        }
    }

    std::sort(choices.begin(), choices.end(), [](const EditorMapChoice& a, const EditorMapChoice& b) {
        return a.label < b.label;
    });
    choices.erase(std::unique(choices.begin(), choices.end(), [](const EditorMapChoice& a, const EditorMapChoice& b) {
        return a.target == b.target;
    }), choices.end());

    m_editorMapChoices = std::move(choices);
    m_editorSelectedMap = -1;
    for (int i = 0; i < static_cast<int>(m_editorMapChoices.size()); ++i)
    {
        const std::string& target = m_editorMapChoices[i].target;
        if (target == m_currentInteriorId ||
            target.find("/" + m_currentInteriorId) != std::string::npos ||
            (!m_loadedPath.empty() && target == m_loadedPath))
        {
            m_editorSelectedMap = i;
            break;
        }
    }
    if (m_editorSelectedMap < 0 && !m_editorMapChoices.empty())
        m_editorSelectedMap = 0;

    m_editorRefreshMapListRequested = false;
    m_status = U8("Seznam map obnoven: ") + std::to_string(m_editorMapChoices.size());
}

void BuildInteriorEngine::renderEditorOverlay()
{
    sanitizeEditorState();
    ImGui::SetNextWindowPos(ImVec2(14.0f, 14.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(650.0f, 800.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(U8("Editor 2.5D interiéru")))
    {
        ImGui::End();
        return;
    }

    // V24: reusable texture browser for sectors, walls and placed objects.
    // Texture keys remain compatible with the renderer, but the editor now
    // exposes their real asset paths instead of forcing manual one-character input.
    auto textureDisplayName = [&](TextureKey key) -> std::string
    {
        const auto it = m_textures.find(key);
        std::string label;
        label += std::to_string(key);
        label += "  |  ";
        if (it != m_textures.end() && !it->second.path.empty())
            label += it->second.path;
        else
            label += U8("procedurální / bez cesty");
        return label;
    };

    auto texturePicker = [&](const char* label, TextureKey& selectedKey) -> bool
    {
        bool changed = false;
        const std::string preview = textureDisplayName(selectedKey);
        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            static char filter[128] = {};
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText(U8("Hledat texturu"), filter, sizeof(filter));
            const std::string filterLower = lowerAscii(filter);

            std::vector<TextureKey> keys;
            keys.reserve(m_textures.size());
            for (const auto& pair : m_textures)
                keys.push_back(pair.first);
            std::sort(keys.begin(), keys.end(), [](TextureKey a, TextureKey b)
            {
                return a < b;
            });

            for (TextureKey key : keys)
            {
                const std::string row = textureDisplayName(key);
                if (!filterLower.empty() &&
                    lowerAscii(row).find(filterLower) == std::string::npos)
                    continue;
                const bool selected = selectedKey == key;
                if (ImGui::Selectable(row.c_str(), selected))
                {
                    selectedKey = key;
                    changed = true;
                    m_sceneDirty = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    };

    ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.46f, 1.0f), U8("Build-like portálové sektory – vlastní implementace"));
    ImGui::TextWrapped("%s", m_loadedPath.c_str());
    ImGui::Separator();

    if (m_editorRefreshMapListRequested || m_editorMapChoices.empty())
        refreshEditorMapList();

    ImGui::TextUnformatted(U8("Načtení mapy projektu"));
    const char* selectedLabel = (m_editorSelectedMap >= 0 &&
        m_editorSelectedMap < static_cast<int>(m_editorMapChoices.size()))
        ? m_editorMapChoices[m_editorSelectedMap].label.c_str()
        : U8("Žádné mapy nenalezeny");
    if (ImGui::BeginCombo("##ProjectMapSelector", selectedLabel))
    {
        for (int i = 0; i < static_cast<int>(m_editorMapChoices.size()); ++i)
        {
            const bool selected = i == m_editorSelectedMap;
            if (ImGui::Selectable(m_editorMapChoices[i].label.c_str(), selected))
                m_editorSelectedMap = i;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button(U8("Načíst vybranou mapu"), ImVec2(180, 0)) &&
        m_editorSelectedMap >= 0 &&
        m_editorSelectedMap < static_cast<int>(m_editorMapChoices.size()))
    {
        m_editorPendingLoadTarget = m_editorMapChoices[m_editorSelectedMap].target;
        m_editorLoadRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(U8("Obnovit seznam"), ImVec2(130, 0)))
        m_editorRefreshMapListRequested = true;

    static char manualMapTarget[320] = "";
    ImGui::SetNextItemWidth(-150.0f);
    ImGui::InputTextWithHint("##ManualMapTarget",
                             U8("castle:houska_1400/nazev_mapy nebo cesta k JSON"),
                             manualMapTarget, sizeof(manualMapTarget));
    ImGui::SameLine();
    if (ImGui::Button(U8("Načíst cestu"), ImVec2(130, 0)) && manualMapTarget[0] != '\0')
    {
        m_editorPendingLoadTarget = manualMapTarget;
        m_editorLoadRequested = true;
    }
    ImGui::TextDisabled(U8("Castle mapy se vždy znovu přeloží ze zdrojového *.map.json."));
    ImGui::Separator();

    char idBuffer[128];
    std::snprintf(idBuffer, sizeof(idBuffer), "%s", m_currentInteriorId.c_str());
    if (ImGui::InputText("Interior ID", idBuffer, sizeof(idBuffer))) m_currentInteriorId = idBuffer;
    char nameBuffer[160];
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", m_displayName.c_str());
    if (ImGui::InputText("Display name", nameBuffer, sizeof(nameBuffer))) m_displayName = nameBuffer;

    float fovDegrees = static_cast<float>(m_fov * 180.0 / kPi);
    if (ImGui::SliderFloat("FOV", &fovDegrees, 45.0f, 95.0f, "%.0f deg")) m_fov = fovDegrees * kPi / 180.0;
    float eyeHeight = static_cast<float>(m_eyeHeight);
    if (ImGui::SliderFloat("Eye height", &eyeHeight, 0.25f, 2.20f, "%.2f")) m_eyeHeight = eyeHeight;

    ImGui::Separator();
    ImGui::TextUnformatted(U8("Výkon náhledu"));
    ImGui::Checkbox(U8("Živý 3D náhled na pozadí"), &m_editorPreview3D);
    if (m_editorPreview3D)
    {
        ImGui::SliderFloat(U8("Rozlišení 3D náhledu"), &m_editorRenderScale,
                           0.15f, 0.60f, "%.2f x");
        ImGui::TextDisabled(U8("Nižší hodnota výrazně zrychlí editor."));
    }
    if (ImGui::SliderFloat(U8("Rozlišení hry"), &m_gameRenderScale,
                           0.20f, 1.00f, "%.2f x"))
        m_sceneDirty = true;
    if (ImGui::Checkbox(U8("Rychlé softwarové vykreslení (2 px podlahy / stěny v plném rozlišení)"),
                        &m_fastFloorCasting))
        m_sceneDirty = true;

    if (ImGui::Button("Save JSON", ImVec2(120, 0))) saveInterior();
    ImGui::SameLine();
    if (ImGui::Button(U8("Reload map/textury"), ImVec2(150, 0))) m_editorReloadRequested = true;
    ImGui::SameLine();
    if (ImGui::Button("Spawn = camera", ImVec2(140, 0)))
    {
        m_editorDefaultSpawnDirty = true;
        m_status = U8("Výchozí spawn se při dalším Save JSON uloží z aktuální kamery.");
    }
    ImGui::SameLine();
    if (ImGui::Button(U8("Obnovit pozici"), ImVec2(140, 0))) recoverToLastSafePosition();

    ImGui::SliderFloat("Map cell px", &m_editorMapCellSize, 10.0f, 36.0f, "%.0f");

    if (ImGui::BeginTabBar("##BuildInteriorTabs"))
    {
        if (ImGui::BeginTabItem("Map"))
        {
            ImGui::TextUnformatted(U8("Levý klik maluje stěnu/dveře, pravý klik přiřadí sektor."));
            const char brushes[] = {'.','1','2','3','4','5','D'};
            const char* labels[] = {"Empty","Stone","Plaster","Brick","Metal","Courtyard wall","Door"};
            for (int i = 0; i < 7; ++i)
            {
                ImGui::PushID(i);
                if (i > 0) ImGui::SameLine();
                if (ImGui::RadioButton(labels[i], m_editorTileBrush == brushes[i])) m_editorTileBrush = brushes[i];
                ImGui::PopID();
            }
            m_editorMapTool = EditorMapTool::Tiles;
            renderEditorMap();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Polygons"))
        {
            renderEditorPolygons();
            ImGui::Separator();
            m_editorMapTool = EditorMapTool::Polygons;
            renderEditorMap();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Sectors"))
        {
            ImGui::TextWrapped(U8("Sektor určuje výšku podlahy a stropu. Rozdíl výšek se kreslí jako portálový dolní/horní segment stěny."));
            std::vector<char> sectorSymbols;
            sectorSymbols.reserve(m_sectors.size());
            for (const auto& pair : m_sectors)
                sectorSymbols.push_back(pair.first);
            std::sort(sectorSymbols.begin(), sectorSymbols.end(),
                      [](char lhs, char rhs) {
                          return sectorCode(lhs) < sectorCode(rhs);
                      });

            const auto selectedSectorIt = m_sectors.find(m_editorSectorBrush);
            const std::string sectorPreview = selectedSectorIt != m_sectors.end()
                ? sectorDisplayLabel(m_editorSectorBrush) + "  |  " +
                  selectedSectorIt->second.name
                : sectorDisplayLabel(m_editorSectorBrush) + U8("  |  nový sektor");
            if (ImGui::BeginCombo(U8("Sektor"), sectorPreview.c_str()))
            {
                for (char symbol : sectorSymbols)
                {
                    const SectorDef& sector = m_sectors.at(symbol);
                    const std::string label =
                        sectorDisplayLabel(symbol) + "  |  " + sector.name;
                    const bool selected = symbol == m_editorSectorBrush;
                    if (ImGui::Selectable(label.c_str(), selected))
                        m_editorSectorBrush = symbol;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button(U8("Přidat sektor")))
            {
                char newSymbol = '\0';
                for (char candidate : sectorSymbolOrder())
                {
                    if (m_sectors.find(candidate) == m_sectors.end())
                    {
                        newSymbol = candidate;
                        break;
                    }
                }
                if (newSymbol == '\0')
                {
                    m_status = U8("Nelze přidat sektor: všech 255 kódů je obsazeno.");
                }
                else
                {
                    SectorDef copy = m_sectors.find(m_editorSectorBrush) != m_sectors.end()
                        ? m_sectors.at(m_editorSectorBrush)
                        : sectorAtPlayer();
                    copy.symbol = newSymbol;
                    copy.id = "sector_" + std::to_string(sectorCode(newSymbol));
                    copy.name = U8("Sektor ") + sectorDisplayLabel(newSymbol);
                    m_sectors[newSymbol] = std::move(copy);
                    m_editorSectorBrush = newSymbol;
                    rebuildSectorLookup();
                    m_sceneDirty = true;
                }
            }
            ImGui::TextDisabled(U8("Zobrazené jsou jen načtené sektory; nový sektor se přidá dalším volným kódem do 255."));

            const bool newSector = m_sectors.find(m_editorSectorBrush) == m_sectors.end();
            SectorDef& s = m_sectors[m_editorSectorBrush];
            if (newSector)
                rebuildSectorLookup();
            s.symbol = m_editorSectorBrush;
            if (s.id.empty())
                s.id = "sector_" + std::to_string(sectorCode(s.symbol));
            if (s.name.empty())
                s.name = U8("Sektor ") + sectorDisplayLabel(s.symbol);
            char sectorName[128]; std::snprintf(sectorName, sizeof(sectorName), "%s", s.name.c_str());
            if (ImGui::InputText("Name", sectorName, sizeof(sectorName))) s.name = sectorName;
            ImGui::TextUnformatted(U8("Materiály sektoru"));
            texturePicker(U8("Textura podlahy"), s.floorTexture);
            texturePicker(U8("Textura stropu"), s.ceilingTexture);
            texturePicker(U8("Textura stěn / portálových segmentů"), s.boundaryTexture);
            ImGui::TextDisabled(U8("Volba se uloží do JSON mapy spolu se sektorem."));

            auto ensureSectorHeadroom = [](SectorDef& sector)
            {
                if (sector.ceilingHeight < sector.floorHeight + 0.35)
                    sector.ceilingHeight = sector.floorHeight + 0.35;
            };
            auto shiftFloorToWorldHeight = [&](SectorDef& sector,
                                               double worldX,
                                               double worldY,
                                               double desiredHeight)
            {
                const double currentHeight = floorHeightAt(sector, worldX, worldY);
                sector.floorHeight += desiredHeight - currentHeight;
                ensureSectorHeadroom(sector);
            };
            auto measureRasterNeighborFloorGaps = [&](const SectorDef& sector,
                                                      double& averageDelta,
                                                      double& maxAbsGap,
                                                      int& samples)
            {
                averageDelta = 0.0;
                maxAbsGap = 0.0;
                samples = 0;
                if (hasVectorGeometry() || m_grid.empty())
                    return false;

                static constexpr int dirs[4][2] = {
                    {1, 0}, {-1, 0}, {0, 1}, {0, -1}
                };
                double sumDelta = 0.0;
                for (int y = 0; y < static_cast<int>(m_grid.size()); ++y)
                {
                    for (int x = 0; x < static_cast<int>(m_grid[y].size()); ++x)
                    {
                        if (isSolidCell(x, y) || sectorSymbolAt(x, y) != sector.symbol)
                            continue;
                        for (const auto& dir : dirs)
                        {
                            const int nx = x + dir[0];
                            const int ny = y + dir[1];
                            if (!isInside(nx, ny) || isSolidCell(nx, ny))
                                continue;
                            const char neighborSymbol = sectorSymbolAt(nx, ny);
                            if (neighborSymbol == sector.symbol)
                                continue;
                            const SectorDef* neighbor =
                                m_sectorLookup[static_cast<unsigned char>(neighborSymbol)];
                            if (!neighbor)
                                continue;

                            const double sampleX =
                                static_cast<double>(x) +
                                (dir[0] > 0 ? 1.0 : (dir[0] < 0 ? 0.0 : 0.5));
                            const double sampleY =
                                static_cast<double>(y) +
                                (dir[1] > 0 ? 1.0 : (dir[1] < 0 ? 0.0 : 0.5));
                            const double delta =
                                floorHeightAt(*neighbor, sampleX, sampleY) -
                                floorHeightAt(sector, sampleX, sampleY);
                            sumDelta += delta;
                            maxAbsGap = std::max(maxAbsGap, std::abs(delta));
                            ++samples;
                        }
                    }
                }
                if (samples <= 0)
                    return false;
                averageDelta = sumDelta / static_cast<double>(samples);
                return true;
            };

            float floorHeight = static_cast<float>(s.floorHeight);
            float ceilingHeight = static_cast<float>(s.ceilingHeight);
            float ambient = static_cast<float>(s.ambient);
            if (ImGui::DragFloat(U8("Základ podlahy v počátku"), &floorHeight, 0.05f, -8.0f, 16.0f, "%.2f"))
            {
                s.floorHeight = floorHeight;
                ensureSectorHeadroom(s);
            }
            float floorAtCamera = static_cast<float>(floorHeightAt(s, m_posX, m_posY));
            if (ImGui::DragFloat(U8("Výška podlahy v pozici kamery"), &floorAtCamera, 0.05f, -8.0f, 16.0f, "%.2f"))
                shiftFloorToWorldHeight(s, m_posX, m_posY, floorAtCamera);
            ImGui::TextDisabled(U8("U ramp je základ v počátku sklonu; tato hodnota posune sektor podle skutečné výšky na mapě."));
            if (ImGui::DragFloat("Ceiling height", &ceilingHeight, 0.05f, -4.0f, 24.0f, "%.2f")) s.ceilingHeight = ceilingHeight;
            ensureSectorHeadroom(s);
            if (ImGui::SliderFloat("Ambient light", &ambient, 0.15f, 1.35f, "%.2f")) s.ambient = ambient;
            float wallHeight = static_cast<float>(s.wallHeight);
            if (ImGui::DragFloat(U8("Výška pevných obvodových zdí"), &wallHeight, 0.05f, -1.0f, 12.0f, "%.2f"))
                s.wallHeight = wallHeight < 0.0f ? -1.0 : std::max(0.35f, wallHeight);
            ImGui::TextDisabled(U8("-1 = výška až ke stropu sektoru. Pro dvůr použij např. 2.8."));
            ImGui::Checkbox(U8("Otevřené nebe / exteriér"), &s.skyCeiling);
            if (s.skyCeiling)
            {
                s.ceilingTexture = 'K';
                if (s.ceilingHeight < s.floorHeight + 8.0) s.ceilingHeight = s.floorHeight + 8.0;
                ImGui::TextDisabled(U8("Strop je nahrazen panoramatickou oblohou K; výšku zdí řídí samostatné pole."));
            }
            if (ImGui::Button(U8("Nastavit jako venkovní sektor")))
            {
                s.skyCeiling = true;
                s.ceilingTexture = 'K';
                s.floorTexture = 'S';
                s.ceilingHeight = s.floorHeight + 12.0;
                s.wallHeight = 2.80;
                s.ambient = 1.10;
            }

            ImGui::Separator();
            ImGui::TextUnformatted(U8("Šikmé plochy – Build styl"));
            float floorSlopeX = static_cast<float>(s.floorSlopeX);
            float floorSlopeY = static_cast<float>(s.floorSlopeY);
            float ceilingSlopeX = static_cast<float>(s.ceilingSlopeX);
            float ceilingSlopeY = static_cast<float>(s.ceilingSlopeY);
            double preservedFloorAtCamera = floorHeightAt(s, m_posX, m_posY);
            if (ImGui::DragFloat(U8("Sklon podlahy X / pole"), &floorSlopeX, 0.01f, -1.0f, 1.0f, "%.3f"))
            {
                s.floorSlopeX = floorSlopeX;
                shiftFloorToWorldHeight(s, m_posX, m_posY, preservedFloorAtCamera);
            }
            preservedFloorAtCamera = floorHeightAt(s, m_posX, m_posY);
            if (ImGui::DragFloat(U8("Sklon podlahy Y / pole"), &floorSlopeY, 0.01f, -1.0f, 1.0f, "%.3f"))
            {
                s.floorSlopeY = floorSlopeY;
                shiftFloorToWorldHeight(s, m_posX, m_posY, preservedFloorAtCamera);
            }
            if (ImGui::DragFloat(U8("Sklon stropu X / pole"), &ceilingSlopeX, 0.01f, -1.0f, 1.0f, "%.3f")) s.ceilingSlopeX = ceilingSlopeX;
            if (ImGui::DragFloat(U8("Sklon stropu Y / pole"), &ceilingSlopeY, 0.01f, -1.0f, 1.0f, "%.3f")) s.ceilingSlopeY = ceilingSlopeY;
            float slopeOriginX = static_cast<float>(s.slopeOriginX);
            float slopeOriginY = static_cast<float>(s.slopeOriginY);
            preservedFloorAtCamera = floorHeightAt(s, m_posX, m_posY);
            if (ImGui::DragFloat(U8("Počátek sklonu X"), &slopeOriginX, 0.10f))
            {
                s.slopeOriginX = slopeOriginX;
                shiftFloorToWorldHeight(s, m_posX, m_posY, preservedFloorAtCamera);
            }
            preservedFloorAtCamera = floorHeightAt(s, m_posX, m_posY);
            if (ImGui::DragFloat(U8("Počátek sklonu Y"), &slopeOriginY, 0.10f))
            {
                s.slopeOriginY = slopeOriginY;
                shiftFloorToWorldHeight(s, m_posX, m_posY, preservedFloorAtCamera);
            }
            if (ImGui::Button(U8("Počátek sklonu = kamera")))
            {
                preservedFloorAtCamera = floorHeightAt(s, m_posX, m_posY);
                s.slopeOriginX = std::floor(m_posX);
                s.slopeOriginY = std::floor(m_posY);
                shiftFloorToWorldHeight(s, m_posX, m_posY, preservedFloorAtCamera);
            }
            ImGui::SameLine();
            if (ImGui::Button(U8("Narovnat plochy")))
            {
                preservedFloorAtCamera = floorHeightAt(s, m_posX, m_posY);
                s.floorSlopeX = s.floorSlopeY = 0.0;
                s.ceilingSlopeX = s.ceilingSlopeY = 0.0;
                shiftFloorToWorldHeight(s, m_posX, m_posY, preservedFloorAtCamera);
            }
            if (ImGui::Button(U8("Rampa na východ +0.15")))
            {
                preservedFloorAtCamera = floorHeightAt(s, m_posX, m_posY);
                s.floorSlopeX = 0.15; s.floorSlopeY = 0.0;
                shiftFloorToWorldHeight(s, m_posX, m_posY, preservedFloorAtCamera);
            }
            ImGui::SameLine();
            if (ImGui::Button(U8("Rampa na západ +0.15")))
            {
                preservedFloorAtCamera = floorHeightAt(s, m_posX, m_posY);
                s.floorSlopeX = -0.15; s.floorSlopeY = 0.0;
                shiftFloorToWorldHeight(s, m_posX, m_posY, preservedFloorAtCamera);
            }
            if (ImGui::Button(U8("Rampa na jih +0.15")))
            {
                preservedFloorAtCamera = floorHeightAt(s, m_posX, m_posY);
                s.floorSlopeX = 0.0; s.floorSlopeY = 0.15;
                shiftFloorToWorldHeight(s, m_posX, m_posY, preservedFloorAtCamera);
            }
            ImGui::SameLine();
            if (ImGui::Button(U8("Rampa na sever +0.15")))
            {
                preservedFloorAtCamera = floorHeightAt(s, m_posX, m_posY);
                s.floorSlopeX = 0.0; s.floorSlopeY = -0.15;
                shiftFloorToWorldHeight(s, m_posX, m_posY, preservedFloorAtCamera);
            }
            ImGui::TextDisabled(U8("0.15 znamená změnu výšky o 0.15 za jedno mapové pole."));

            ImGui::Separator();
            ImGui::TextUnformatted(U8("Navazování podlahy"));
            const SectorDef& cameraSector = sectorAtWorld(m_posX, m_posY);
            if (cameraSector.symbol != s.symbol &&
                ImGui::Button(U8("Převzít sklon ze sektoru pod kamerou")))
            {
                const double targetHeight = floorHeightAt(cameraSector, m_posX, m_posY);
                s.floorSlopeX = cameraSector.floorSlopeX;
                s.floorSlopeY = cameraSector.floorSlopeY;
                s.slopeOriginX = cameraSector.slopeOriginX;
                s.slopeOriginY = cameraSector.slopeOriginY;
                shiftFloorToWorldHeight(s, m_posX, m_posY, targetHeight);
            }
            double averageFloorDelta = 0.0;
            double maxFloorGap = 0.0;
            int floorGapSamples = 0;
            const bool hasNeighborGaps =
                measureRasterNeighborFloorGaps(s, averageFloorDelta, maxFloorGap, floorGapSamples);
            if (hasNeighborGaps)
            {
                ImGui::TextDisabled(U8("Sousední hrany: %d vzorků, max. mezera %.2f, posun %.2f"),
                                    floorGapSamples, maxFloorGap, averageFloorDelta);
                if (ImGui::Button(U8("Dorovnat výšku na sousední sektory")))
                {
                    s.floorHeight += averageFloorDelta;
                    ensureSectorHeadroom(s);
                    m_status = U8("Výška podlahy sektoru byla dorovnána na sousední hrany.");
                }
            }
            else
            {
                ImGui::TextDisabled(U8("Pro vybraný sektor nejsou v rastrové mapě nalezené průchozí sousední hrany."));
            }

            ImGui::SliderInt("Floor R", &s.floorColorR, 0, 255); ImGui::SameLine();
            ImGui::SliderInt("Floor G", &s.floorColorG, 0, 255); ImGui::SameLine();
            ImGui::SliderInt("Floor B", &s.floorColorB, 0, 255);
            ImGui::SliderInt("Ceiling R", &s.ceilingColorR, 0, 255); ImGui::SameLine();
            ImGui::SliderInt("Ceiling G", &s.ceilingColorG, 0, 255); ImGui::SameLine();
            ImGui::SliderInt("Ceiling B", &s.ceilingColorB, 0, 255);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stairs"))
        {
            ImGui::TextWrapped(U8("Schodiště se vytvoří jako řada skutečných výškových sektorů. Každý stupeň má vlastní podlahu a portálový renderer vykreslí jeho čelo."));
            const char* directions[] = {U8("Sever"), U8("Východ"), U8("Jih"), U8("Západ")};
            ImGui::Combo(U8("Směr od kamery"), &m_editorStairDirection, directions, 4);
            ImGui::SliderInt(U8("Počet stupňů"), &m_editorStairSteps, 2, 12);
            ImGui::SliderInt(U8("Šířka"), &m_editorStairWidth, 1, 3);
            ImGui::DragFloat(U8("Celková změna výšky"), &m_editorStairTotalRise, 0.05f, -4.0f, 4.0f, "%.2f");
            ImGui::Checkbox(U8("Strop kopíruje schody"), &m_editorStairCeilingFollows);
            int firstSectorCode = sectorCode(m_editorStairFirstSector);
            if (ImGui::DragInt(U8("První sektorový kód"), &firstSectorCode, 1.0f, 1, 255))
            {
                m_editorStairFirstSector =
                    static_cast<char>(static_cast<unsigned char>(
                        std::clamp(firstSectorCode, 1, 255)));
            }
            ImGui::SameLine();
            if (ImGui::Button(U8("Najít volné")))
            {
                const int steps = std::clamp(m_editorStairSteps, 2, 12);
                auto rangeFree = [&](int start)
                {
                    for (int offset = 0; offset < steps; ++offset)
                    {
                        const int code = start + offset;
                        if (m_sectors.find(static_cast<char>(
                                static_cast<unsigned char>(code))) != m_sectors.end())
                            return false;
                    }
                    return true;
                };
                int foundStart = 0;
                for (int start = static_cast<int>('A');
                     start + steps - 1 <= 255; ++start)
                {
                    if (rangeFree(start))
                    {
                        foundStart = start;
                        break;
                    }
                }
                for (int start = 1;
                     foundStart == 0 && start + steps - 1 <= 255; ++start)
                {
                    if (rangeFree(start))
                        foundStart = start;
                }
                if (foundStart > 0)
                {
                    m_editorStairFirstSector =
                        static_cast<char>(static_cast<unsigned char>(foundStart));
                }
                else
                {
                    m_status = U8("Nenalezen volný souvislý rozsah sektorů pro schody.");
                }
            }
            ImGui::TextDisabled(U8("Začátek je jedno pole před kamerou. Použijí se po sobě jdoucí byte kódy sektorů."));
            if (ImGui::Button(U8("Vytvořit schodiště od kamery"), ImVec2(260.0f, 0.0f)))
                generateEditorStairs();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Doors"))
        {
            for (int i = 0; i < static_cast<int>(m_doors.size()); ++i)
            {
                ImGui::PushID(i);
                const DoorDef& d = m_doors[i];
                const std::string row = d.id + " [" + std::to_string(d.x) + "," + std::to_string(d.y) + "]";
                if (ImGui::Selectable(row.c_str(), m_editorSelectedDoor == i)) m_editorSelectedDoor = i;
                ImGui::PopID();
            }
            if (m_editorSelectedDoor >= 0 && m_editorSelectedDoor < static_cast<int>(m_doors.size()))
            {
                DoorDef& d = m_doors[m_editorSelectedDoor];
                char id[128]; std::snprintf(id, sizeof(id), "%s", d.id.c_str());
                if (ImGui::InputText("Door ID", id, sizeof(id))) d.id = id;
                ImGui::Checkbox("Locked", &d.locked);
                if (d.motion != DoorMotion::Transition)
                    ImGui::Checkbox("Open", &d.targetOpen);

                int motionIndex = d.motion == DoorMotion::Swing ? 0
                    : d.motion == DoorMotion::Slide ? 1
                    : d.motion == DoorMotion::Raise ? 2 : 3;
                const char* motions[] = {"Swing (hinged)", "Slide", "Raise (Build-style)", "Transition portal"};
                if (ImGui::Combo("Motion", &motionIndex, motions, 4))
                    d.motion = motionIndex == 0 ? DoorMotion::Swing
                        : motionIndex == 1 ? DoorMotion::Slide
                        : motionIndex == 2 ? DoorMotion::Raise
                        : DoorMotion::Transition;

                int textureKeyEdit = static_cast<int>(d.texture);
                if (ImGui::InputInt("Texture key", &textureKeyEdit))
                    d.texture = static_cast<TextureKey>(std::clamp(textureKeyEdit, 0, 65535));
                ImGui::SliderInt("Span", &d.span, 1, 8);
                int axisIndex = d.axis == 'y' ? 1 : 0;
                const char* axes[] = {"X", "Y"};
                if (ImGui::Combo("Span axis", &axisIndex, axes, 2)) d.axis = axisIndex == 1 ? 'y' : 'x';
                float doorHeight = static_cast<float>(d.height);
                if (ImGui::DragFloat("Door height", &doorHeight, 0.05f, -1.0f, 8.0f, "%.2f")) d.height = doorHeight;
                if (d.motion == DoorMotion::Swing)
                {
                    int hingeIndex = d.hingeAtEnd ? 1 : 0;
                    const char* hinges[] = {"Start", "End"};
                    if (ImGui::Combo("Hinge", &hingeIndex, hinges, 2)) d.hingeAtEnd = hingeIndex == 1;
                    int swingDirection = d.swingDirection < 0.0 ? 1 : 0;
                    const char* directions[] = {"Positive", "Negative"};
                    if (ImGui::Combo("Swing side", &swingDirection, directions, 2))
                        d.swingDirection = swingDirection == 1 ? -1.0 : 1.0;
                    float swingDegrees = static_cast<float>(d.swingDegrees);
                    if (ImGui::SliderFloat("Open angle", &swingDegrees, 30.0f, 150.0f, "%.0f deg"))
                        d.swingDegrees = swingDegrees;
                    float thickness = static_cast<float>(d.thickness);
                    if (ImGui::SliderFloat("Leaf thickness", &thickness, 0.02f, 0.22f, "%.2f"))
                        d.thickness = thickness;
                }

                float speed = static_cast<float>(d.speed);
                if (ImGui::SliderFloat("Speed", &speed, 0.15f, 8.0f, "%.2f")) d.speed = speed;
                char target[128]; std::snprintf(target, sizeof(target), "%s", d.targetInterior.c_str());
                if (ImGui::InputText("Target interior", target, sizeof(target))) d.targetInterior = target;
                char targetSpawn[128]; std::snprintf(targetSpawn, sizeof(targetSpawn), "%s", d.targetSpawn.c_str());
                if (ImGui::InputText("Target spawn", targetSpawn, sizeof(targetSpawn))) d.targetSpawn = targetSpawn;
                ImGui::Text(U8("Otevření: %.0f %%"), d.openAmount * 100.0);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Objects"))
        {
            ImGui::TextWrapped(U8("Objekty můžeš umisťovat jako billboard sprity nebo jako voxelové objekty. Nový objekt vznikne na pozici kamery a následně jej lze přesně doladit níže."));
            static TextureKey newObjectTexture = static_cast<TextureKey>('B');
            static int newObjectRenderMode = 0;
            texturePicker(U8("Textura nového objektu"), newObjectTexture);
            const char* newObjectModes[] = {
                U8("Obecný billboard"),
                U8("Obecný voxel"),
                U8("Obecný interaktivní marker")
            };
            ImGui::Combo(U8("Typ nového objektu"), &newObjectRenderMode, newObjectModes, 3);
            if (ImGui::Button(U8("Přidat obecný objekt"), ImVec2(190, 0)))
            {
                SpriteDef s;
                s.id = makeSpriteId();
                s.texture = newObjectTexture;
                s.x = m_posX;
                s.y = m_posY;
                s.zOffset = 0.0;
                s.scale = newObjectRenderMode == 2 ? 0.45 : 1.0;
                s.renderMode = newObjectRenderMode == 1
                    ? ObjectRenderMode::Voxel : ObjectRenderMode::Billboard;
                s.voxelDepth = newObjectRenderMode == 1 ? 0.30 : 0.0;
                if (newObjectRenderMode == 2)
                    s.interactionLabel = U8("E – použít");
                m_sprites.push_back(s);
                m_editorSelectedSprite = static_cast<int>(m_sprites.size()) - 1;
                m_sceneDirty = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(U8("umístění = aktuální kamera"));
            ImGui::Separator();
            m_editorMapTool = EditorMapTool::Objects;
            renderEditorMap();
            ImGui::Separator();

            for (int i = 0; i < static_cast<int>(m_sprites.size()); ++i)
            {
                ImGui::PushID(1000 + i);
                if (ImGui::Selectable(m_sprites[i].id.c_str(), m_editorSelectedSprite == i)) m_editorSelectedSprite = i;
                ImGui::PopID();
            }
            if (m_editorSelectedSprite >= 0 && m_editorSelectedSprite < static_cast<int>(m_sprites.size()))
            {
                SpriteDef& s = m_sprites[m_editorSelectedSprite];
                bool objectChanged = false;
                char id[128]; std::snprintf(id, sizeof(id), "%s", s.id.c_str());
                if (ImGui::InputText("Object ID", id, sizeof(id))) { s.id = id; objectChanged = true; }
                if (texturePicker(U8("Textura objektu"), s.texture)) objectChanged = true;
                float px = static_cast<float>(s.x), py = static_cast<float>(s.y), pz = static_cast<float>(s.zOffset), scale = static_cast<float>(s.scale);
                if (ImGui::DragFloat("X", &px, 0.05f)) { s.x = px; objectChanged = true; }
                if (ImGui::DragFloat("Y", &py, 0.05f)) { s.y = py; objectChanged = true; }
                if (ImGui::DragFloat("Z offset", &pz, 0.02f)) { s.zOffset = pz; objectChanged = true; }
                if (ImGui::SliderFloat("Scale", &scale, 0.10f, 4.0f, "%.2f")) { s.scale = scale; objectChanged = true; }
                if (ImGui::Checkbox("Solid", &s.solid)) objectChanged = true;
                int renderMode = s.renderMode == ObjectRenderMode::Voxel ? 1 : 0;
                const char* renderModes[] = {"Billboard", "Voxel 3D"};
                if (ImGui::Combo("Render mode", &renderMode, renderModes, 2))
                {
                    s.renderMode = renderMode == 1 ? ObjectRenderMode::Voxel : ObjectRenderMode::Billboard;
                    objectChanged = true;
                }
                if (s.renderMode == ObjectRenderMode::Voxel)
                {
                    float yawDegrees = static_cast<float>(s.yaw * 180.0 / kPi);
                    if (ImGui::SliderFloat("Voxel yaw", &yawDegrees, -180.0f, 180.0f, "%.0f deg"))
                    {
                        s.yaw = yawDegrees * kPi / 180.0;
                        objectChanged = true;
                    }
                    float voxelDepth = static_cast<float>(s.voxelDepth);
                    if (ImGui::SliderFloat("Voxel depth (0 = auto)", &voxelDepth, 0.0f, 2.0f, "%.2f"))
                    {
                        s.voxelDepth = voxelDepth;
                        objectChanged = true;
                    }
                    const TextureRef* voxelTexture = m_textureLookup[static_cast<std::size_t>(s.texture)];
                    if (voxelTexture)
                        ImGui::TextDisabled(U8("Voxelová mřížka: %d x %d, %d buněk"),
                            voxelTexture->voxelGridW, voxelTexture->voxelGridH,
                            static_cast<int>(voxelTexture->voxelCells.size()));
                }

                ImGui::Separator();
                ImGui::TextUnformatted(U8("Animace a světlo"));
                if (ImGui::Checkbox(U8("Animovaný objekt"), &s.animated)) objectChanged = true;
                ImGui::SameLine();
                if (ImGui::Checkbox(U8("Náhodné pořadí/časování"), &s.randomAnimation)) objectChanged = true;
                if (s.animated)
                {
                    float minFps = static_cast<float>(s.animationMinFps);
                    float maxFps = static_cast<float>(s.animationMaxFps);
                    if (ImGui::SliderFloat(U8("Min. FPS animace"), &minFps, 1.0f, 24.0f, "%.1f"))
                    {
                        s.animationMinFps = minFps;
                        objectChanged = true;
                    }
                    if (ImGui::SliderFloat(U8("Max. FPS animace"), &maxFps, minFps, 30.0f, "%.1f"))
                    {
                        s.animationMaxFps = maxFps;
                        objectChanged = true;
                    }
                    const TextureRef* animatedTexture = m_textureLookup[static_cast<std::size_t>(s.texture)];
                    const int frameCount = animatedTexture
                        ? static_cast<int>(animatedTexture->animationFrames.size()) : 0;
                    ImGui::TextDisabled(U8("Nalezené animační snímky: %d | aktuální: %d"),
                                        frameCount, s.animationFrame);
                }

                if (ImGui::Checkbox(U8("Vyzařuje světlo"), &s.emitsLight)) objectChanged = true;
                if (s.emitsLight)
                {
                    float lightColor[3] = {s.lightR / 255.0f, s.lightG / 255.0f, s.lightB / 255.0f};
                    if (ImGui::ColorEdit3(U8("Barva světla"), lightColor))
                    {
                        s.lightR = static_cast<int>(std::clamp(lightColor[0], 0.0f, 1.0f) * 255.0f);
                        s.lightG = static_cast<int>(std::clamp(lightColor[1], 0.0f, 1.0f) * 255.0f);
                        s.lightB = static_cast<int>(std::clamp(lightColor[2], 0.0f, 1.0f) * 255.0f);
                        objectChanged = true;
                    }
                    float radius = static_cast<float>(s.lightRadius);
                    float intensity = static_cast<float>(s.lightIntensity);
                    float height = static_cast<float>(s.lightHeight);
                    float flicker = static_cast<float>(s.lightFlicker);
                    if (ImGui::SliderFloat(U8("Dosah světla"), &radius, 0.2f, 10.0f, "%.2f")) { s.lightRadius = radius; objectChanged = true; }
                    if (ImGui::SliderFloat(U8("Intenzita světla"), &intensity, 0.0f, 3.0f, "%.2f")) { s.lightIntensity = intensity; objectChanged = true; }
                    if (ImGui::SliderFloat(U8("Výška zdroje"), &height, -0.5f, 4.0f, "%.2f")) { s.lightHeight = height; objectChanged = true; }
                    if (ImGui::SliderFloat(U8("Míra mihotání"), &flicker, 0.0f, 0.65f, "%.2f")) { s.lightFlicker = flicker; objectChanged = true; }
                }

                char label[160]; std::snprintf(label, sizeof(label), "%s", s.interactionLabel.c_str());
                if (ImGui::InputText("Interaction label", label, sizeof(label))) { s.interactionLabel = label; objectChanged = true; }
                char target[128]; std::snprintf(target, sizeof(target), "%s", s.targetInterior.c_str());
                if (ImGui::InputText("Target interior", target, sizeof(target))) { s.targetInterior = target; objectChanged = true; }
                char targetSpawn[128]; std::snprintf(targetSpawn, sizeof(targetSpawn), "%s", s.targetSpawn.c_str());
                if (ImGui::InputText("Target spawn", targetSpawn, sizeof(targetSpawn))) { s.targetSpawn = targetSpawn; objectChanged = true; }
                if (objectChanged)
                    m_sceneDirty = true;
                if (ImGui::Button(U8("Přesunout ke kameře"))) { s.x = m_posX; s.y = m_posY; m_sceneDirty = true; }
                ImGui::SameLine();
                if (ImGui::Button(U8("Duplikovat objekt")))
                {
                    SpriteDef copy = s;
                    copy.id = makeSpriteId();
                    copy.x += 0.35;
                    copy.y += 0.35;
                    m_sprites.push_back(copy);
                    m_editorSelectedSprite = static_cast<int>(m_sprites.size()) - 1;
                    m_sceneDirty = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(U8("Smazat objekt")))
                {
                    m_sprites.erase(m_sprites.begin() + m_editorSelectedSprite);
                    m_editorSelectedSprite = -1;
                    m_sceneDirty = true;
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Spawns"))
        {
            static char spawnName[96] = "entry";
            ImGui::InputText("Spawn name", spawnName, sizeof(spawnName));
            if (ImGui::Button(U8("Uložit kameru jako spawn")) && spawnName[0] != '\0')
            {
                SpawnDef spawn;
                spawn.x = m_posX; spawn.y = m_posY; spawn.angle = m_angle; spawn.pitch = m_pitch;
                m_namedSpawns[spawnName] = spawn;
                m_status = U8("Spawn uložen: ") + std::string(spawnName);
            }
            ImGui::Separator();
            std::string eraseSpawn;
            for (auto& pair : m_namedSpawns)
            {
                ImGui::PushID(pair.first.c_str());
                ImGui::Text("%s  [%.2f, %.2f]", pair.first.c_str(), pair.second.x, pair.second.y);
                ImGui::SameLine();
                if (ImGui::SmallButton(U8("Přejít")))
                {
                    m_posX = pair.second.x; m_posY = pair.second.y;
                    m_angle = pair.second.angle; m_pitch = pair.second.pitch;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(U8("Přepsat")))
                {
                    pair.second.x = m_posX; pair.second.y = m_posY;
                    pair.second.angle = m_angle; pair.second.pitch = m_pitch;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(U8("Smazat"))) eraseSpawn = pair.first;
                ImGui::PopID();
            }
            if (!eraseSpawn.empty()) m_namedSpawns.erase(eraseSpawn);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::TextUnformatted(U8("Kamera: W/S, A/D, šipky, mezerník skok. Home obnoví bezpečnou pozici. ESC zpět do menu."));
    if (!m_status.empty()) ImGui::TextColored(ImVec4(0.66f, 0.92f, 0.58f, 1.0f), "%s", m_status.c_str());
    if (!m_lastError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.35f, 1.0f), "%s", m_lastError.c_str());
    ImGui::End();

    // Loading destroys and rebuilds maps, sectors, textures and editor lists.
    // Doing that in the middle of an ImGui window can invalidate references
    // still used by the current frame. Defer reload until the window is closed.
    if (m_editorRefreshMapListRequested)
        refreshEditorMapList();

    if (m_editorLoadRequested)
    {
        m_editorLoadRequested = false;
        const std::string target = m_editorPendingLoadTarget;
        m_editorPendingLoadTarget.clear();
        if (!target.empty())
        {
            const bool loaded = loadInterior(target);
            if (loaded)
            {
                sanitizeEditorState();
                refreshEditorMapList();
                m_sceneDirty = true;
            }
            else
            {
                // Transactional loading keeps the current map alive. Do not
                // sanitize or rebuild editor selections against a failed map.
                m_status = U8("Mapa nebyla načtena; editor ponechal předchozí mapu.");
            }
        }
    }
    else if (m_editorReloadRequested)
    {
        m_editorReloadRequested = false;
        const std::string reloadTarget = m_loadedCastleId.empty()
            ? (m_loadedPath.empty() ? m_currentInteriorId : m_loadedPath)
            : ("castle:" + m_loadedCastleId + "/" + m_loadedCastleMapId);
        if (loadInterior(reloadTarget))
        {
            sanitizeEditorState();
            m_sceneDirty = true;
        }
        else
        {
            m_status = U8("Reload selhal; editor ponechal předchozí mapu.");
        }
    }
}

void BuildInteriorEngine::renderEditorPolygons()
{
    ImGui::TextWrapped(U8("Polygonální sektory jsou skutečná geometrie enginu. Každý prostor je definovaný třemi nebo více body; čtvercová mřížka už není zdrojem stěn."));

    if (ImGui::Button(U8("Nový polygon u kamery")))
    {
        PolygonSectorRegion region;
        region.id = "polygon_" + std::to_string(m_polygonSectors.size() + 1);
        region.sector = m_editorSectorBrush;
        region.vertices = {
            {m_posX - 2.0, m_posY - 2.0},
            {m_posX + 2.0, m_posY - 2.0},
            {m_posX + 2.0, m_posY + 2.0},
            {m_posX - 2.0, m_posY + 2.0}
        };
        updatePolygonBounds(region);
        m_polygonSectors.push_back(std::move(region));
        m_editorSelectedPolygon = static_cast<int>(m_polygonSectors.size()) - 1;
        m_editorSelectedPolygonVertex = -1;
        m_editorSelectedOpening = -1;
        rebuildPolygonBoundaryWalls();
        rebuildVectorSectorLookup();
        m_sceneDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(U8("Uložit geometry JSON")))
        saveCastleGeometryOverlay();

    const char* preview = m_editorSelectedPolygon >= 0 &&
                          m_editorSelectedPolygon < static_cast<int>(m_polygonSectors.size())
        ? m_polygonSectors[static_cast<std::size_t>(m_editorSelectedPolygon)].id.c_str()
        : U8("Vyber polygon");
    if (ImGui::BeginCombo(U8("Polygon"), preview))
    {
        for (int i = 0; i < static_cast<int>(m_polygonSectors.size()); ++i)
        {
            const bool selected = i == m_editorSelectedPolygon;
            if (ImGui::Selectable(m_polygonSectors[static_cast<std::size_t>(i)].id.c_str(), selected))
            {
                m_editorSelectedPolygon = i;
                m_editorSelectedPolygonVertex = -1;
                m_editorSelectedOpening = -1;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (m_editorSelectedPolygon < 0 ||
        m_editorSelectedPolygon >= static_cast<int>(m_polygonSectors.size()))
        return;

    PolygonSectorRegion& region = m_polygonSectors[static_cast<std::size_t>(m_editorSelectedPolygon)];
    if (!polygonIsSimple(region))
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.28f, 1.0f),
                           "%s", U8("Polygon se protíná nebo má nulovou plochu; stěny se z něj negenerují."));
    bool changed = false;
    char idBuffer[128]{};
    std::snprintf(idBuffer, sizeof(idBuffer), "%s", region.id.c_str());
    if (ImGui::InputText("ID", idBuffer, sizeof(idBuffer)))
    {
        region.id = idBuffer;
        changed = true;
    }

    std::vector<char> polygonSectorSymbols;
    polygonSectorSymbols.reserve(m_sectors.size());
    for (const auto& pair : m_sectors)
        polygonSectorSymbols.push_back(pair.first);
    std::sort(polygonSectorSymbols.begin(), polygonSectorSymbols.end(),
              [](char lhs, char rhs) {
                  return sectorCode(lhs) < sectorCode(rhs);
              });
    const auto polygonSectorIt = m_sectors.find(region.sector);
    const std::string polygonSectorPreview = polygonSectorIt != m_sectors.end()
        ? sectorDisplayLabel(region.sector) + "  |  " + polygonSectorIt->second.name
        : sectorDisplayLabel(region.sector);
    if (ImGui::BeginCombo(U8("Sektor"), polygonSectorPreview.c_str()))
    {
        for (char symbol : polygonSectorSymbols)
        {
            const SectorDef& sector = m_sectors.at(symbol);
            const std::string label =
                sectorDisplayLabel(symbol) + "  |  " + sector.name;
            const bool selected = symbol == region.sector;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                region.sector = symbol;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    changed |= ImGui::Checkbox(U8("Vytvořit stěny po obvodu"), &region.boundarySolid);
    int wallTextureKey = static_cast<int>(region.wallTexture);
    if (ImGui::InputInt(U8("Textura stěny (0 = sektor)"), &wallTextureKey))
    {
        region.wallTexture = static_cast<TextureKey>(std::clamp(wallTextureKey, 0, 65535));
        changed = true;
    }
    float wallAmbient = static_cast<float>(region.wallAmbient);
    if (ImGui::SliderFloat(U8("Jas stěny"), &wallAmbient, 0.15f, 1.35f, "%.2f"))
    {
        region.wallAmbient = wallAmbient;
        changed = true;
    }
    float wallScale = static_cast<float>(region.wallTextureScale);
    if (ImGui::SliderFloat(U8("Měřítko textury stěny"), &wallScale, 0.10f, 4.0f, "%.2f"))
    {
        region.wallTextureScale = wallScale;
        changed = true;
    }

    ImGui::Checkbox(U8("Přidávat body kliknutím do mapy"), &m_editorPolygonPointMode);
    ImGui::SameLine();
    if (ImGui::Button(U8("Smazat polygon")))
    {
        m_polygonSectors.erase(m_polygonSectors.begin() + m_editorSelectedPolygon);
        m_editorSelectedPolygon = -1;
        m_editorSelectedPolygonVertex = -1;
        m_editorSelectedOpening = -1;
        rebuildPolygonBoundaryWalls();
        rebuildVectorSectorLookup();
        m_sceneDirty = true;
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted(U8("Vrcholové body"));
    for (int i = 0; i < static_cast<int>(region.vertices.size()); ++i)
    {
        ImGui::PushID(1000 + i);
        char label[64]{};
        std::snprintf(label, sizeof(label), "%d: %.2f, %.2f", i,
                      region.vertices[static_cast<std::size_t>(i)][0],
                      region.vertices[static_cast<std::size_t>(i)][1]);
        if (ImGui::Selectable(label, m_editorSelectedPolygonVertex == i))
            m_editorSelectedPolygonVertex = i;
        ImGui::PopID();
    }

    if (m_editorSelectedPolygonVertex >= 0 &&
        m_editorSelectedPolygonVertex < static_cast<int>(region.vertices.size()))
    {
        auto& vertex = region.vertices[static_cast<std::size_t>(m_editorSelectedPolygonVertex)];
        double x = vertex[0];
        double y = vertex[1];
        if (ImGui::InputDouble("X", &x, 0.10, 1.0, "%.3f")) { vertex[0] = x; changed = true; }
        if (ImGui::InputDouble("Y", &y, 0.10, 1.0, "%.3f")) { vertex[1] = y; changed = true; }
        if (ImGui::Button(U8("Vložit bod za vybraný")))
        {
            const int next = (m_editorSelectedPolygonVertex + 1) % static_cast<int>(region.vertices.size());
            const auto& b = region.vertices[static_cast<std::size_t>(next)];
            region.vertices.insert(region.vertices.begin() + m_editorSelectedPolygonVertex + 1,
                                   {(vertex[0] + b[0]) * 0.5, (vertex[1] + b[1]) * 0.5});
            ++m_editorSelectedPolygonVertex;
            changed = true;
        }
        ImGui::SameLine();
        if (region.vertices.size() > 3 && ImGui::Button(U8("Smazat bod")))
        {
            region.vertices.erase(region.vertices.begin() + m_editorSelectedPolygonVertex);
            m_editorSelectedPolygonVertex = -1;
            changed = true;
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted(U8("Otvory ve stěnách"));
    ImGui::TextDisabled(U8("Hrana 0 vede od bodu 0 k bodu 1, poslední hrana uzavírá polygon."));
    if (ImGui::Button(U8("Přidat dveřní otvor")))
    {
        PolygonEdgeOpening opening;
        opening.edge = std::max(0, m_editorSelectedPolygonVertex);
        if (!region.vertices.empty()) opening.edge %= static_cast<int>(region.vertices.size());
        region.openings.push_back(opening);
        m_editorSelectedOpening = static_cast<int>(region.openings.size()) - 1;
        changed = true;
    }
    for (int i = 0; i < static_cast<int>(region.openings.size()); ++i)
    {
        ImGui::PushID(2000 + i);
        const PolygonEdgeOpening& opening = region.openings[static_cast<std::size_t>(i)];
        char label[80]{};
        std::snprintf(label, sizeof(label), "otvor %d | hrana %d | %.2f-%.2f",
                      i, opening.edge, opening.start, opening.end);
        if (ImGui::Selectable(label, m_editorSelectedOpening == i))
            m_editorSelectedOpening = i;
        ImGui::PopID();
    }
    if (m_editorSelectedOpening >= 0 &&
        m_editorSelectedOpening < static_cast<int>(region.openings.size()))
    {
        PolygonEdgeOpening& opening = region.openings[static_cast<std::size_t>(m_editorSelectedOpening)];
        changed |= ImGui::InputInt(U8("Hrana otvoru"), &opening.edge);
        float start = static_cast<float>(opening.start);
        float end = static_cast<float>(opening.end);
        float bottom = static_cast<float>(opening.bottom);
        float height = static_cast<float>(opening.height);
        if (ImGui::SliderFloat(U8("Začátek otvoru"), &start, 0.0f, 1.0f, "%.2f")) { opening.start = start; changed = true; }
        if (ImGui::SliderFloat(U8("Konec otvoru"), &end, 0.0f, 1.0f, "%.2f")) { opening.end = end; changed = true; }
        if (ImGui::InputFloat(U8("Spodní hrana nad podlahou"), &bottom, 0.05f, 0.25f, "%.2f")) { opening.bottom = std::max(0.0f, bottom); changed = true; }
        if (ImGui::InputFloat(U8("Výška otvoru"), &height, 0.05f, 0.25f, "%.2f")) { opening.height = std::max(0.10f, height); changed = true; }
        if (ImGui::Button(U8("Smazat otvor")))
        {
            region.openings.erase(region.openings.begin() + m_editorSelectedOpening);
            m_editorSelectedOpening = -1;
            changed = true;
        }
    }

    if (changed)
    {
        for (PolygonEdgeOpening& opening : region.openings)
        {
            if (!region.vertices.empty())
                opening.edge = std::clamp(opening.edge, 0, static_cast<int>(region.vertices.size()) - 1);
            opening.start = std::clamp(opening.start, 0.0, 1.0);
            opening.end = std::clamp(opening.end, 0.0, 1.0);
            if (opening.end < opening.start) std::swap(opening.start, opening.end);
        }
        updatePolygonBounds(region);
        rebuildPolygonBoundaryWalls();
        rebuildVectorSectorLookup();
        m_sceneDirty = true;
    }
}

void BuildInteriorEngine::renderEditorMap()
{
    const bool vectorMode = hasVectorGeometry();
    if (m_grid.empty() && !vectorMode) return;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float cellSize = vectorMode ? std::min(m_editorMapCellSize, 14.0f)
                                      : m_editorMapCellSize;

    double minWorldX = 0.0;
    double minWorldY = 0.0;
    double maxWorldX = 1.0;
    double maxWorldY = 1.0;
    if (vectorMode)
    {
        minWorldX = m_polygonSectors.front().minX;
        minWorldY = m_polygonSectors.front().minY;
        maxWorldX = m_polygonSectors.front().maxX;
        maxWorldY = m_polygonSectors.front().maxY;
        for (const PolygonSectorRegion& region : m_polygonSectors)
        {
            minWorldX = std::min(minWorldX, region.minX);
            minWorldY = std::min(minWorldY, region.minY);
            maxWorldX = std::max(maxWorldX, region.maxX);
            maxWorldY = std::max(maxWorldY, region.maxY);
        }
        minWorldX = std::floor(minWorldX) - 1.0;
        minWorldY = std::floor(minWorldY) - 1.0;
        maxWorldX = std::ceil(maxWorldX) + 1.0;
        maxWorldY = std::ceil(maxWorldY) + 1.0;
    }
    else
    {
        maxWorldY = static_cast<double>(m_grid.size());
        maxWorldX = 0.0;
        for (const auto& row : m_grid)
            maxWorldX = std::max(maxWorldX, static_cast<double>(row.size()));
    }

    const int columns = std::max(1, static_cast<int>(std::ceil(maxWorldX - minWorldX)));
    const int rows = std::max(1, static_cast<int>(std::ceil(maxWorldY - minWorldY)));
    const ImVec2 size(columns * cellSize, rows * cellSize);
    ImGui::InvisibleButton("##BuildInteriorMap", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const double mouseWorldX = minWorldX + (mouse.x - origin.x) / cellSize;
    const double mouseWorldY = minWorldY + (mouse.y - origin.y) / cellSize;
    const int mouseX = static_cast<int>(std::floor(mouseWorldX));
    const int mouseY = static_cast<int>(std::floor(mouseWorldY));
    const bool shiftDown = ImGui::GetIO().KeyShift;

    auto nearestSpriteAt = [&](double worldX, double worldY) {
        int best = -1;
        double bestDistance = kHuge;
        for (int i = 0; i < static_cast<int>(m_sprites.size()); ++i)
        {
            const SpriteDef& sprite = m_sprites[static_cast<std::size_t>(i)];
            const double radius = std::max(0.55, sprite.scale * 0.55);
            const double distance = std::hypot(sprite.x - worldX,
                                               sprite.y - worldY);
            if (distance <= radius && distance < bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    };
    auto moveSelectedSpriteTo = [&](double worldX, double worldY) {
        if (m_editorSelectedSprite < 0 ||
            m_editorSelectedSprite >= static_cast<int>(m_sprites.size()))
            return false;
        SpriteDef& sprite =
            m_sprites[static_cast<std::size_t>(m_editorSelectedSprite)];
        sprite.x = std::round(worldX * 4.0) / 4.0;
        sprite.y = std::round(worldY * 4.0) / 4.0;
        m_sceneDirty = true;
        return true;
    };

    if (hovered && shiftDown && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const int pickedSprite = nearestSpriteAt(mouseWorldX, mouseWorldY);
        if (pickedSprite >= 0)
        {
            m_editorSelectedSprite = pickedSprite;
            m_status = U8("Vybrán objekt: ") +
                m_sprites[static_cast<std::size_t>(pickedSprite)].id;
        }
        else if (m_editorMapTool == EditorMapTool::Objects &&
                 moveSelectedSpriteTo(mouseWorldX, mouseWorldY))
        {
            m_status = U8("Objekt přesunut Shift-klikem.");
        }
        else if (!vectorMode && isInside(mouseX, mouseY))
        {
            m_editorTileBrush = cellAt(mouseX, mouseY);
            m_editorSectorBrush = sectorSymbolAt(mouseX, mouseY);
            m_status = U8("Převzaty vlastnosti buňky: sektor ") +
                sectorDisplayLabel(m_editorSectorBrush);
        }
        else if (vectorMode)
        {
            for (int polygonIndex =
                     static_cast<int>(m_polygonSectors.size()) - 1;
                 polygonIndex >= 0; --polygonIndex)
            {
                const PolygonSectorRegion& region =
                    m_polygonSectors[static_cast<std::size_t>(polygonIndex)];
                if (!pointInPolygon(region, mouseWorldX, mouseWorldY))
                    continue;
                m_editorSelectedPolygon = polygonIndex;
                m_editorSelectedPolygonVertex = -1;
                m_editorSelectedOpening = -1;
                m_editorSectorBrush = region.sector;
                m_status = U8("Vybrán polygonový sektor ") +
                    sectorDisplayLabel(region.sector);
                break;
            }
        }
    }

    if (hovered && vectorMode && !shiftDown)
    {
        if (m_editorPolygonPointMode &&
            m_editorSelectedPolygon >= 0 &&
            m_editorSelectedPolygon < static_cast<int>(m_polygonSectors.size()))
        {
            PolygonSectorRegion& region = m_polygonSectors[static_cast<std::size_t>(m_editorSelectedPolygon)];
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const double snappedX = std::round(mouseWorldX * 4.0) / 4.0;
                const double snappedY = std::round(mouseWorldY * 4.0) / 4.0;
                region.vertices.push_back({snappedX, snappedY});
                m_editorSelectedPolygonVertex = static_cast<int>(region.vertices.size()) - 1;
                updatePolygonBounds(region);
                rebuildPolygonBoundaryWalls();
                rebuildVectorSectorLookup();
                m_sceneDirty = true;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !region.vertices.empty())
            {
                double bestDistance = 0.45;
                int best = -1;
                for (int i = 0; i < static_cast<int>(region.vertices.size()); ++i)
                {
                    const double dx = region.vertices[static_cast<std::size_t>(i)][0] - mouseWorldX;
                    const double dy = region.vertices[static_cast<std::size_t>(i)][1] - mouseWorldY;
                    const double distance = std::hypot(dx, dy);
                    if (distance < bestDistance) { bestDistance = distance; best = i; }
                }
                m_editorSelectedPolygonVertex = best;
            }
        }
        else
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                double bestDistance = 0.55;
                int bestPolygon = -1;
                int bestVertex = -1;
                for (int polygonIndex = 0; polygonIndex < static_cast<int>(m_polygonSectors.size()); ++polygonIndex)
                {
                    const PolygonSectorRegion& region = m_polygonSectors[static_cast<std::size_t>(polygonIndex)];
                    for (int vertexIndex = 0; vertexIndex < static_cast<int>(region.vertices.size()); ++vertexIndex)
                    {
                        const auto& vertex = region.vertices[static_cast<std::size_t>(vertexIndex)];
                        const double distance = std::hypot(vertex[0] - mouseWorldX,
                                                           vertex[1] - mouseWorldY);
                        if (distance < bestDistance)
                        {
                            bestDistance = distance;
                            bestPolygon = polygonIndex;
                            bestVertex = vertexIndex;
                        }
                    }
                }
                if (bestPolygon >= 0)
                {
                    m_editorSelectedPolygon = bestPolygon;
                    m_editorSelectedPolygonVertex = bestVertex;
                    m_editorSelectedOpening = -1;
                }
            }
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                m_editorSelectedPolygon >= 0 &&
                m_editorSelectedPolygon < static_cast<int>(m_polygonSectors.size()))
            {
                PolygonSectorRegion& region = m_polygonSectors[static_cast<std::size_t>(m_editorSelectedPolygon)];
                if (m_editorSelectedPolygonVertex >= 0 &&
                    m_editorSelectedPolygonVertex < static_cast<int>(region.vertices.size()))
                {
                    auto& vertex = region.vertices[static_cast<std::size_t>(m_editorSelectedPolygonVertex)];
                    const double snappedX = std::round(mouseWorldX * 4.0) / 4.0;
                    const double snappedY = std::round(mouseWorldY * 4.0) / 4.0;
                    if (std::abs(vertex[0] - snappedX) > 1.0e-8 ||
                        std::abs(vertex[1] - snappedY) > 1.0e-8)
                    {
                        vertex = {snappedX, snappedY};
                        updatePolygonBounds(region);
                        // Keep the 2D editor responsive while dragging. The
                        // expensive sub-cell cache and 3D preview are rebuilt
                        // once when the mouse button is released.
                        m_editorPolygonDragDirty = true;
                    }
                }
            }
        }
        if (m_editorPolygonDragDirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_editorPolygonDragDirty = false;
            rebuildPolygonBoundaryWalls();
            rebuildVectorSectorLookup();
            m_sceneDirty = true;
        }
    }
    else if (hovered && !vectorMode && !shiftDown && isInside(mouseX, mouseY))
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) paintEditorCell(mouseX, mouseY, false);
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) paintEditorCell(mouseX, mouseY, true);
    }

    auto screenPoint = [&](double worldX, double worldY) {
        return ImVec2(origin.x + static_cast<float>(worldX - minWorldX) * cellSize,
                      origin.y + static_cast<float>(worldY - minWorldY) * cellSize);
    };

    if (!vectorMode)
    {
        for (int y = 0; y < static_cast<int>(m_grid.size()); ++y)
        {
            for (int x = 0; x < static_cast<int>(m_grid[y].size()); ++x)
            {
                const TextureKey tile = cellAt(x, y);
                const SectorDef& sector = sectorAt(x, y);
                ImU32 fill = imguiColor(sector.floorColorR, sector.floorColorG, sector.floorColorB, 220);
                if (!isEmptyCell(tile))
                {
                    if (tile == '1') fill = imguiColor(96, 92, 84);
                    else if (tile == '2') fill = imguiColor(150, 132, 104);
                    else if (tile == '3') fill = imguiColor(126, 58, 42);
                    else if (tile == '4') fill = imguiColor(82, 84, 86);
                    else if (tile == 'D') fill = imguiColor(142, 84, 44);
                    else fill = imguiColor(75, 72, 68);
                }
                const ImVec2 p0 = screenPoint(x, y);
                const ImVec2 p1(p0.x + cellSize - 1.0f, p0.y + cellSize - 1.0f);
                draw->AddRectFilled(p0, p1, fill);
                draw->AddRect(p0, p1, imguiColor(25, 22, 18, 220));
                const char symbol = sectorSymbolAt(x, y);
                const std::string sectorText =
                    sectorCode(symbol) >= 33 && sectorCode(symbol) <= 126
                        ? std::string(1, symbol)
                        : std::to_string(sectorCode(symbol));
                draw->AddText(ImVec2(p0.x + 3.0f, p0.y + 2.0f),
                              imguiColor(255, 232, 150), sectorText.c_str());
            }
        }
    }
    else
    {
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                            imguiColor(28, 27, 24, 245));
        for (int x = 0; x <= columns; x += 2)
        {
            const float px = origin.x + x * cellSize;
            draw->AddLine(ImVec2(px, origin.y), ImVec2(px, origin.y + size.y),
                          imguiColor(74, 70, 62, 55));
        }
        for (int y = 0; y <= rows; y += 2)
        {
            const float py = origin.y + y * cellSize;
            draw->AddLine(ImVec2(origin.x, py), ImVec2(origin.x + size.x, py),
                          imguiColor(74, 70, 62, 55));
        }
    }

    for (int polygonIndex = 0; polygonIndex < static_cast<int>(m_polygonSectors.size()); ++polygonIndex)
    {
        const PolygonSectorRegion& region = m_polygonSectors[static_cast<std::size_t>(polygonIndex)];
        if (region.vertices.size() < 3) continue;
        const bool validPolygon = polygonIsSimple(region);
        const SectorDef* sector = m_sectorLookup[static_cast<unsigned char>(region.sector)];
        const ImU32 color = !validPolygon
            ? imguiColor(255, 70, 70, 235)
            : (sector ? imguiColor(sector->floorColorR, sector->floorColorG, sector->floorColorB, 225)
                      : imguiColor(110, 170, 230, 225));
        std::vector<ImVec2> points;
        points.reserve(region.vertices.size());
        for (const auto& vertex : region.vertices)
            points.push_back(screenPoint(vertex[0], vertex[1]));
        bool convex = true;
        double turnSign = 0.0;
        for (std::size_t i = 0; i < region.vertices.size() && convex; ++i)
        {
            const auto& a = region.vertices[i];
            const auto& b = region.vertices[(i + 1) % region.vertices.size()];
            const auto& c = region.vertices[(i + 2) % region.vertices.size()];
            const double cross = (b[0] - a[0]) * (c[1] - b[1]) -
                                 (b[1] - a[1]) * (c[0] - b[0]);
            if (std::abs(cross) < 1.0e-8) continue;
            if (turnSign == 0.0) turnSign = cross;
            else if ((turnSign > 0.0) != (cross > 0.0)) convex = false;
        }
        if (validPolygon && convex && points.size() >= 3)
            draw->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()),
                                      (color & 0x00ffffffu) | 0x68000000u);
        const bool selected = polygonIndex == m_editorSelectedPolygon;
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            draw->AddLine(points[i], points[(i + 1) % points.size()],
                          selected ? imguiColor(255, 190, 82) : color,
                          selected ? 3.0f : 1.8f);
            draw->AddCircleFilled(points[i], selected ? 4.0f : 2.5f,
                                  selected && static_cast<int>(i) == m_editorSelectedPolygonVertex
                                      ? imguiColor(255, 90, 70)
                                      : imguiColor(255, 224, 145));
        }
    }

    // Explicit special-purpose wall segments remain visible in purple. Polygon
    // boundary walls are not drawn separately because the polygon outline is
    // already the authoritative geometry and duplicate lines only add clutter.
    for (const WallSegmentDef& wall : m_wallSegments)
    {
        const ImVec2 a = screenPoint(wall.x0, wall.y0);
        const ImVec2 b = screenPoint(wall.x1, wall.y1);
        draw->AddLine(a, b, imguiColor(190, 105, 255), 2.0f);
    }

    for (int i = 0; i < static_cast<int>(m_sprites.size()); ++i)
    {
        const SpriteDef& sprite = m_sprites[static_cast<std::size_t>(i)];
        const ImVec2 point = screenPoint(sprite.x, sprite.y);
        if (i == m_editorSelectedSprite)
        {
            double aspect = 1.0;
            if (const TextureRef* texture =
                    m_textureLookup[static_cast<std::size_t>(sprite.texture)])
            {
                if (texture->w > 0 && texture->h > 0)
                    aspect = static_cast<double>(texture->w) / texture->h;
            }
            const double width = std::max(0.40, sprite.scale * aspect);
            const double depth = sprite.renderMode == ObjectRenderMode::Voxel
                ? std::max(0.30, sprite.voxelDepth > 0.0
                    ? sprite.voxelDepth
                    : sprite.scale * 0.32)
                : std::max(0.30, sprite.scale * 0.28);
            const double minX = sprite.x - width * 0.5;
            const double maxX = sprite.x + width * 0.5;
            const double minY = sprite.y - depth * 0.5;
            const double maxY = sprite.y + depth * 0.5;
            const ImVec2 p0 = screenPoint(minX, minY);
            const ImVec2 p1 = screenPoint(maxX, maxY);
            draw->AddRectFilled(p0, p1, imguiColor(255, 210, 82, 34));
            draw->AddRect(p0, p1, imguiColor(255, 210, 82, 245), 0.0f, 0, 2.0f);
            for (int gx = static_cast<int>(std::ceil(minX));
                 gx <= static_cast<int>(std::floor(maxX)); ++gx)
            {
                const ImVec2 a = screenPoint(gx, minY);
                const ImVec2 b = screenPoint(gx, maxY);
                draw->AddLine(a, b, imguiColor(255, 210, 82, 120));
            }
            for (int gy = static_cast<int>(std::ceil(minY));
                 gy <= static_cast<int>(std::floor(maxY)); ++gy)
            {
                const ImVec2 a = screenPoint(minX, gy);
                const ImVec2 b = screenPoint(maxX, gy);
                draw->AddLine(a, b, imguiColor(255, 210, 82, 120));
            }
            draw->AddText(ImVec2(point.x + 7.0f, point.y - 18.0f),
                          imguiColor(255, 232, 150), sprite.id.c_str());
        }
        draw->AddCircleFilled(point, std::max(3.0f, cellSize * 0.16f),
                              i == m_editorSelectedSprite
                                  ? imguiColor(255, 95, 65)
                                  : imguiColor(95, 215, 255));
    }

    const ImVec2 player = screenPoint(m_posX, m_posY);
    draw->AddCircleFilled(player, std::max(3.0f, cellSize * 0.15f), imguiColor(255, 226, 108));
    draw->AddLine(player,
                  screenPoint(m_posX + std::cos(m_angle) * 0.8,
                              m_posY + std::sin(m_angle) * 0.8),
                  imguiColor(255, 226, 108), 2.0f);

    if (hovered)
        ImGui::SetTooltip("world %.2f / %.2f", mouseWorldX, mouseWorldY);
}

void BuildInteriorEngine::paintEditorCell(int x, int y, bool sectorOnly)
{
    if (!isInside(x, y)) return;
    ensureSectorGrid();
    if (sectorOnly)
    {
        m_sectorGrid[y][x] = m_editorSectorBrush;
        m_sceneDirty = true;
        return;
    }

    const TextureKey previous = m_grid[y][x];
    m_grid[y][x] = m_editorTileBrush;
    m_sectorGrid[y][x] = m_editorSectorBrush;
    if (m_editorTileBrush == static_cast<TextureKey>('D'))
    {
        if (!doorAt(x, y))
        {
            DoorDef d; d.x = x; d.y = y; d.id = makeDoorId(x, y);
            d.motion = DoorMotion::Swing;
            m_doors.push_back(d);
            m_editorSelectedDoor = static_cast<int>(m_doors.size()) - 1;
        }
    }
    else if (previous == 'D')
    {
        m_doors.erase(std::remove_if(m_doors.begin(), m_doors.end(), [this, x, y](const DoorDef& d) {
            return doorCoversCell(d, x, y);
        }), m_doors.end());
        m_editorSelectedDoor = -1;
    }
    m_sceneDirty = true;
}

void BuildInteriorEngine::generateEditorStairs()
{
    if (m_grid.empty())
    {
        m_status = U8("Nelze vytvořit schody: mapa je prázdná.");
        return;
    }

    ensureSectorGrid();

    const int dirX[4] = {0, 1, 0, -1};
    const int dirY[4] = {-1, 0, 1, 0};
    const int sideX[4] = {1, 0, 1, 0};
    const int sideY[4] = {0, 1, 0, 1};
    const int direction = std::clamp(m_editorStairDirection, 0, 3);
    const int steps = std::clamp(m_editorStairSteps, 2, 12);
    const int width = std::clamp(m_editorStairWidth, 1, 3);

    const int firstSectorCode = sectorCode(m_editorStairFirstSector);
    if (firstSectorCode + steps - 1 > 255)
    {
        m_status = U8("Schody potřebují více volných symbolů sektorů.");
        return;
    }
    const int startX = static_cast<int>(std::floor(m_posX)) + dirX[direction];
    const int startY = static_cast<int>(std::floor(m_posY)) + dirY[direction];
    const SectorDef base = sectorAtPlayer();
    const double clearHeight = std::max(kPlayerBodyHeight + 0.20,
                                        base.ceilingHeight - base.floorHeight);

    struct StairCell { int x; int y; int step; char symbol; };
    std::vector<StairCell> cells;
    cells.reserve(static_cast<std::size_t>(steps * width));

    for (int step = 0; step < steps; ++step)
    {
        const char symbol = static_cast<char>(
            static_cast<unsigned char>(firstSectorCode + step));
        for (int lane = 0; lane < width; ++lane)
        {
            const int centeredLane = lane - (width - 1) / 2;
            const int x = startX + dirX[direction] * step + sideX[direction] * centeredLane;
            const int y = startY + dirY[direction] * step + sideY[direction] * centeredLane;
            if (!isInside(x, y))
            {
                m_status = U8("Schody se nevejdou do mapy; nic nebylo změněno.");
                return;
            }
            if (!isEmptyCell(cellAt(x, y)))
            {
                m_status = U8("V trase schodů je stěna nebo dveře; nic nebylo změněno.");
                return;
            }
            cells.push_back({x, y, step, symbol});
        }
    }

    auto isTargetCell = [&](int x, int y) {
        return std::any_of(cells.begin(), cells.end(), [&](const StairCell& c) {
            return c.x == x && c.y == y;
        });
    };

    // Never silently redefine a sector that is already used elsewhere. The old
    // implementation could overwrite E/F/G and change unrelated rooms while
    // the editor was rendering them.
    for (int step = 0; step < steps; ++step)
    {
        const char symbol = static_cast<char>(
            static_cast<unsigned char>(firstSectorCode + step));
        for (int y = 0; y < static_cast<int>(m_sectorGrid.size()); ++y)
        {
            for (int x = 0; x < static_cast<int>(m_sectorGrid[y].size()); ++x)
            {
                if (m_sectorGrid[y][x] == symbol && !isTargetCell(x, y))
                {
                    m_status = U8("Sektor ") + sectorDisplayLabel(symbol) +
                               U8(" už mapa používá. Vyber jiný první sektor.");
                    return;
                }
            }
        }
    }

    for (int step = 0; step < steps; ++step)
    {
        const char symbol = static_cast<char>(
            static_cast<unsigned char>(firstSectorCode + step));
        SectorDef stairSector = base;
        stairSector.symbol = symbol;
        stairSector.id = "stair_" + std::to_string(step + 1);
        stairSector.name = U8("Schod ") + std::to_string(step + 1);
        stairSector.floorHeight = base.floorHeight +
            static_cast<double>(m_editorStairTotalRise) * (step + 1) / steps;
        stairSector.ceilingHeight = m_editorStairCeilingFollows
            ? stairSector.floorHeight + clearHeight
            : std::max(base.ceilingHeight,
                       stairSector.floorHeight + kPlayerBodyHeight + kMinimumHeadroom);
        stairSector.skyCeiling = false;
        m_sectors[symbol] = std::move(stairSector);
    }

    rebuildSectorLookup();
    for (const StairCell& cell : cells)
    {
        m_grid[cell.y][cell.x] = kNoTexture;
        m_sectorGrid[cell.y][cell.x] = cell.symbol;
    }

    m_sceneDirty = true;
    m_status = U8("Vytvořeno schodiště: ") + std::to_string(cells.size()) + U8(" polí.");
}

void BuildInteriorEngine::ensureSectorGrid()
{
    if (m_grid.empty()) return;
    if (m_sectorGrid.size() != m_grid.size()) m_sectorGrid.assign(m_grid.size(), std::string());
    for (int y = 0; y < static_cast<int>(m_grid.size()); ++y)
    {
        if (m_sectorGrid[y].size() != m_grid[y].size()) m_sectorGrid[y].assign(m_grid[y].size(), 'A');
        for (char& c : m_sectorGrid[y]) if (c == '\0') c = 'A';
    }
}

void BuildInteriorEngine::ensureDefaultSectors()
{
    SectorDef a; a.symbol='A'; a.id="great_hall"; a.name=U8("Velká kamenná síň");
    a.floorTexture='F'; a.ceilingTexture='C'; a.boundaryTexture='1'; a.floorHeight=0.0; a.ceilingHeight=1.15; a.ambient=0.95;
    m_sectors['A']=a;

    SectorDef b; b.symbol='B'; b.id="raised_room"; b.name=U8("Vyvýšená světnice");
    b.floorTexture='F'; b.ceilingTexture='P'; b.boundaryTexture='2'; b.floorHeight=0.22; b.ceilingHeight=1.25; b.ambient=1.08;
    b.floorColorR=82; b.floorColorG=54; b.floorColorB=34; b.ceilingColorR=90; b.ceilingColorG=80; b.ceilingColorB=66;
    m_sectors['B']=b;

    SectorDef c; c.symbol='C'; c.id="cellar"; c.name=U8("Nízký sklep");
    c.floorTexture='S'; c.ceilingTexture='C'; c.boundaryTexture='1'; c.floorHeight=-0.35; c.ceilingHeight=0.72; c.ambient=0.52;
    c.floorColorR=42; c.floorColorG=42; c.floorColorB=39; c.ceilingColorR=22; c.ceilingColorG=22; c.ceilingColorB=23;
    m_sectors['C']=c;

    SectorDef d; d.symbol='D'; d.id="gallery"; d.name=U8("Horní galerie");
    d.floorTexture='F'; d.ceilingTexture='C'; d.boundaryTexture='3'; d.floorHeight=0.55; d.ceilingHeight=1.75; d.ambient=0.82;
    m_sectors['D']=d;

    SectorDef o; o.symbol='O'; o.id="outdoor"; o.name=U8("Venkovní dvůr");
    o.floorTexture='Y'; o.ceilingTexture='K'; o.boundaryTexture='5'; o.floorHeight=0.0; o.ceilingHeight=12.0; o.ambient=1.10; o.wallHeight=2.80; o.skyCeiling=true;
    o.floorColorR=72; o.floorColorG=68; o.floorColorB=58; o.ceilingColorR=88; o.ceilingColorG=105; o.ceilingColorB=120;
    m_sectors['O']=o;
    rebuildSectorLookup();
}

void BuildInteriorEngine::rebuildDoorsFromGridIfMissing()
{
    for (int y = 0; y < static_cast<int>(m_grid.size()); ++y)
    {
        for (int x = 0; x < static_cast<int>(m_grid[y].size()); ++x)
        {
            if (m_grid[y][x] == static_cast<TextureKey>('D') && !doorAt(x, y))
            {
                DoorDef d; d.x = x; d.y = y; d.id = makeDoorId(x, y);
                d.motion = DoorMotion::Swing;
                m_doors.push_back(d);
            }
        }
    }
}

std::string BuildInteriorEngine::makeDoorId(int x, int y) const
{
    return "door_" + std::to_string(x) + "_" + std::to_string(y);
}

std::string BuildInteriorEngine::makeSpriteId() const
{
    int number = 1;
    for (;; ++number)
    {
        const std::string id = "interior_object_" + std::to_string(number);
        const bool exists = std::any_of(m_sprites.begin(), m_sprites.end(), [&](const SpriteDef& s) { return s.id == id; });
        if (!exists) return id;
    }
}

std::uint32_t BuildInteriorEngine::sampleTexture(TextureKey key, double u, double v, std::uint32_t fallback) const
{
    const TextureRef* texture = m_textureLookup[static_cast<std::size_t>(key)];
    if (!texture || texture->w <= 0 || texture->h <= 0 || texture->pixels.empty())
        return fallback;
    u -= std::floor(u);
    v -= std::floor(v);
    const int x = std::clamp(static_cast<int>(u * texture->w), 0, texture->w - 1);
    const int y = std::clamp(static_cast<int>(v * texture->h), 0, texture->h - 1);
    return texture->pixels[static_cast<std::size_t>(y) * texture->w + x];
}

void BuildInteriorEngine::putPixel(int x, int y, std::uint32_t color)
{
    if (x < 0 || y < 0 || x >= m_sceneW || y >= m_sceneH) return;
    m_framebuffer[static_cast<std::size_t>(y) * m_sceneW + x] = color;
}

void BuildInteriorEngine::drawTexturedColumn(int x, int y0, int y1, TextureKey textureKey, double u,
                                              double distance, double ambient, int clipTop, int clipBottom,
                                              double vStart, double vEnd, int pixelWidth,
                                              double lightWorldX, double lightWorldY, double lightWorldZ)
{
    if (y1 < y0) std::swap(y0, y1);
    const int originalStart = y0;
    const int originalEnd = std::max(y0 + 1, y1);
    const int start = std::max({0, clipTop, y0});
    const int end = std::min({m_sceneH - 1, clipBottom, y1});
    const int width = std::clamp(pixelWidth, 1, std::max(1, m_sceneW - x));
    if (end < start || x < 0 || x >= m_sceneW) return;

    const double shade = std::clamp(ambient / (1.0 + distance * 0.075), 0.18, 1.15);
    const TextureRef* texture = m_textureLookup[static_cast<std::size_t>(textureKey)];
    const std::uint32_t fallback = applyWorldLighting(argb(130, 120, 105), shade,
                                                       lightWorldX, lightWorldY, lightWorldZ);

    int texX = 0;
    if (texture && texture->w > 0 && texture->h > 0 && !texture->pixels.empty())
    {
        u -= std::floor(u);
        texX = std::clamp(static_cast<int>(u * texture->w), 0, texture->w - 1);
    }

    for (int y = start; y <= end; ++y)
    {
        std::uint32_t color = fallback;
        if (texture && texture->w > 0 && texture->h > 0 && !texture->pixels.empty())
        {
            const double t = static_cast<double>(y - originalStart) /
                             std::max(1, originalEnd - originalStart);
            double v = vStart + (vEnd - vStart) * t;
            v -= std::floor(v);
            const int texY = std::clamp(static_cast<int>(v * texture->h), 0, texture->h - 1);
            color = applyWorldLighting(
                texture->pixels[static_cast<std::size_t>(texY) * texture->w + texX],
                shade, lightWorldX, lightWorldY, lightWorldZ);
        }
        const std::size_t row = static_cast<std::size_t>(y) * m_sceneW + x;
        for (int dx = 0; dx < width; ++dx)
        {
            const std::size_t index = row + static_cast<std::size_t>(dx);
            if (distance >= m_dynamicDepthBuffer[index]) continue;
            m_framebuffer[index] = color;
            m_dynamicDepthBuffer[index] = distance;
        }
    }
}

void BuildInteriorEngine::drawFlatTriangle(const ProjectedVertex& a, const ProjectedVertex& b,
                                             const ProjectedVertex& c, std::uint32_t color)
{
    const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(area) < 1.0e-8) return;
    const int minX = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int maxX = std::min(m_sceneW - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int maxY = std::min(m_sceneH - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    const double invA = 1.0 / a.depth;
    const double invB = 1.0 / b.depth;
    const double invC = 1.0 / c.depth;

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
            const double w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
            const double w2 = 1.0 - w0 - w1;
            if (w0 < -1.0e-5 || w1 < -1.0e-5 || w2 < -1.0e-5) continue;
            const double invDepth = w0 * invA + w1 * invB + w2 * invC;
            if (invDepth <= 1.0e-10) continue;
            const double depth = 1.0 / invDepth;
            if (depth >= m_zBuffer[x]) continue;
            const std::size_t index = static_cast<std::size_t>(y) * m_sceneW + x;
            if (depth >= m_dynamicDepthBuffer[index]) continue;
            m_framebuffer[index] = alphaBlend(m_framebuffer[index], color);
            m_dynamicDepthBuffer[index] = depth;
        }
    }
}

void BuildInteriorEngine::drawTexturedTriangle(const ProjectedVertex& a, const ProjectedVertex& b,
                                                 const ProjectedVertex& c, const TextureRef& texture,
                                                 double shade)
{
    const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(area) < 1.0e-8 || texture.w <= 0 || texture.h <= 0 || texture.pixels.empty()) return;
    const int minX = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int maxX = std::min(m_sceneW - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int maxY = std::min(m_sceneH - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    const double invA = 1.0 / a.depth;
    const double invB = 1.0 / b.depth;
    const double invC = 1.0 / c.depth;

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
            const double w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
            const double w2 = 1.0 - w0 - w1;
            if (w0 < -1.0e-5 || w1 < -1.0e-5 || w2 < -1.0e-5) continue;
            const double invDepth = w0 * invA + w1 * invB + w2 * invC;
            if (invDepth <= 1.0e-10) continue;
            const double depth = 1.0 / invDepth;
            if (depth >= m_zBuffer[x]) continue;
            const std::size_t index = static_cast<std::size_t>(y) * m_sceneW + x;
            if (depth >= m_dynamicDepthBuffer[index]) continue;
            double u = (w0 * a.u * invA + w1 * b.u * invB + w2 * c.u * invC) / invDepth;
            double v = (w0 * a.v * invA + w1 * b.v * invB + w2 * c.v * invC) / invDepth;
            u = std::clamp(u, 0.0, 0.999999);
            v = std::clamp(v, 0.0, 0.999999);
            const int tx = std::clamp(static_cast<int>(u * texture.w), 0, texture.w - 1);
            const int ty = std::clamp(static_cast<int>(v * texture.h), 0, texture.h - 1);
            std::uint32_t color = texture.pixels[static_cast<std::size_t>(ty) * texture.w + tx];
            if (((color >> 24u) & 0xffu) < 8u) continue;
            color = modulate(color, shade);
            m_framebuffer[index] = alphaBlend(m_framebuffer[index], color);
            m_dynamicDepthBuffer[index] = depth;
        }
    }
}

void BuildInteriorEngine::drawFlatQuad(const ProjectedVertex& a, const ProjectedVertex& b,
                                        const ProjectedVertex& c, const ProjectedVertex& d,
                                        std::uint32_t color)
{
    drawFlatTriangle(a, b, c, color);
    drawFlatTriangle(a, c, d, color);
}

void BuildInteriorEngine::drawTexturedQuad(const ProjectedVertex& a, const ProjectedVertex& b,
                                            const ProjectedVertex& c, const ProjectedVertex& d,
                                            const TextureRef& texture, double shade)
{
    drawTexturedTriangle(a, b, c, texture, shade);
    drawTexturedTriangle(a, c, d, texture, shade);
}

int BuildInteriorEngine::projectZ(double worldZ, double distance, int screenH, int horizon, double eyeZ) const
{
    distance = std::max(0.02, distance);
    double projected = horizon - (worldZ - eyeZ) * screenH / distance;
    if (!std::isfinite(projected))
        return horizon;
    projected = std::clamp(projected, -1000000.0, 1000000.0);
    return static_cast<int>(std::lround(projected));
}

std::string BuildInteriorEngine::resolveInteriorPath(const std::string& interiorIdOrPath)
{
    if (isPathLike(interiorIdOrPath))
    {
        fs::path path(interiorIdOrPath);
        if (path.is_absolute()) return path.lexically_normal().string();
        return (ProjectRootPath() / path).lexically_normal().string();
    }
    return (ProjectRootPath() / "data" / "interiors" / (interiorIdOrPath + ".json")).lexically_normal().string();
}

std::string BuildInteriorEngine::resolveProjectPath(const std::string& relativePath)
{
    fs::path path(relativePath);
    if (path.is_absolute()) return path.lexically_normal().string();
    return (ProjectRootPath() / path).lexically_normal().string();
}
