#pragma once

#include <SDL.h>
#include <SDL_image.h>
#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

// Lightweight in-engine texture/sprite editor for Houska runtime assets.
// Toggle with F8. It deliberately uses the project's SDL2 + ImGui stack,
// so no external Python utility is required.
class TextureSpriteEditor
{
public:
    bool init(SDL_Renderer* renderer, const std::filesystem::path& projectRoot)
    {
        shutdown();
        m_renderer = renderer;
        m_assetDir = projectRoot / "assets" / "Castles" / "Houska1400";
        refreshFileList();
        return m_renderer != nullptr;
    }

    void shutdown()
    {
        destroyPreview();
        destroyAnimationPreview();
        destroyOnionSkinPreview();
        freeSurface();
        m_files.clear();
        m_animationFiles.clear();
        m_renderer = nullptr;
        m_open = false;
    }

    void handleEvent(const SDL_Event& e)
    {
        if (e.type == SDL_KEYDOWN && e.key.repeat == 0 &&
            e.key.keysym.scancode == SDL_SCANCODE_F8)
        {
            m_open = !m_open;
        }
    }

    bool isOpen() const { return m_open; }
    void open() { m_open = true; }
    void close() { m_open = false; }

    void update(float dt)
    {
        if (!m_open || !m_animationPlaying || m_animationFiles.empty())
            return;

        m_animationAccumulator += std::max(0.0f, dt);
        const float frameTime = 1.0f / std::max(1.0f, m_animationFps);
        while (m_animationAccumulator >= frameTime)
        {
            m_animationAccumulator -= frameTime;
            ++m_animationIndex;
            if (m_animationIndex >= static_cast<int>(m_animationFiles.size()))
            {
                if (m_animationLoop)
                    m_animationIndex = 0;
                else
                {
                    m_animationIndex = static_cast<int>(m_animationFiles.size()) - 1;
                    m_animationPlaying = false;
                }
            }
            rebuildAnimationPreview();
        }
    }

    bool consumeReloadRequested()
    {
        const bool value = m_reloadRequested;
        m_reloadRequested = false;
        return value;
    }

    void render()
    {
        if (!m_open)
            return;

        ImGui::SetNextWindowSize(ImVec2(1080.0f, 720.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Texture, Sprite & Animation Editor", &m_open,
                          ImGuiWindowFlags_MenuBar))
        {
            ImGui::End();
            return;
        }

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::MenuItem("Refresh assets"))
                refreshFileList();
            if (ImGui::MenuItem("Reload current map after save", nullptr, m_autoReload))
                m_autoReload = !m_autoReload;
            if (ImGui::BeginMenu("Layout"))
            {
                if (ImGui::MenuItem("Reset panel sizes"))
                {
                    m_assetPanelWidth = 270.0f;
                    m_operationsPanelHeight = 220.0f;
                }
                ImGui::Separator();
                ImGui::SetNextItemWidth(220.0f);
                ImGui::SliderFloat("Asset panel width", &m_assetPanelWidth,
                    170.0f, std::max(170.0f, ImGui::GetIO().DisplaySize.x - 420.0f), "%.0f px");
                ImGui::SetNextItemWidth(220.0f);
                ImGui::SliderFloat("Operations panel height", &m_operationsPanelHeight,
                    180.0f, std::max(180.0f, ImGui::GetIO().DisplaySize.y - 300.0f), "%.0f px");
                ImGui::TextDisabled("Drag the highlighted handles or use the sliders above.");
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        const ImVec2 editorAvail = ImGui::GetContentRegionAvail();
        const float splitterThickness = 6.0f;
        const float minAssetWidth = 170.0f;
        const float minWorkWidth = 360.0f;
        m_assetPanelWidth = std::clamp(
            m_assetPanelWidth,
            minAssetWidth,
            std::max(minAssetWidth, editorAvail.x - minWorkWidth - splitterThickness));

        ImGui::BeginChild("asset_list", ImVec2(m_assetPanelWidth, 0.0f), true);
        ImGui::TextUnformatted("assets/Castles/Houska1400");
        ImGui::Separator();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filter", "Filter PNG...", m_filter, sizeof(m_filter));
        for (int i = 0; i < static_cast<int>(m_files.size()); ++i)
        {
            const std::string name = m_files[static_cast<std::size_t>(i)].filename().string();
            if (!containsNoCase(name, m_filter))
                continue;
            const bool selected = i == m_selectedIndex;
            if (ImGui::Selectable(name.c_str(), selected))
                loadIndex(i);
        }
        ImGui::EndChild();

        ImGui::SameLine(0.0f, 0.0f);
        drawVerticalSplitter("asset_splitter", splitterThickness,
            m_assetPanelWidth, minAssetWidth, minWorkWidth);
        ImGui::SameLine(0.0f, 0.0f);

        ImGui::BeginGroup();
        const ImVec2 workAvail = ImGui::GetContentRegionAvail();
        const float minPreviewHeight = 180.0f;
        const float minOperationsHeight = 180.0f;
        m_operationsPanelHeight = std::clamp(
            m_operationsPanelHeight,
            minOperationsHeight,
            std::max(minOperationsHeight,
                workAvail.y - minPreviewHeight - splitterThickness));
        const float previewHeight = std::max(
            minPreviewHeight,
            workAvail.y - m_operationsPanelHeight - splitterThickness);

        ImGui::BeginChild("preview", ImVec2(0.0f, previewHeight), true,
                          ImGuiWindowFlags_HorizontalScrollbar);

        if (m_surface && m_preview)
        {
            ImGui::Text("%s | %d x %d | %s",
                        m_currentPath.filename().string().c_str(),
                        m_surface->w, m_surface->h,
                        m_dirty ? "modified" : "saved");
            ImGui::SliderFloat("Preview scale", &m_previewScale, 0.25f, 6.0f, "%.2fx");
            SDL_Texture* shownTexture =
                (!m_animationFiles.empty() && m_animationPreview)
                    ? m_animationPreview
                    : m_preview;
            const int shownW = (!m_animationFiles.empty() && m_animationPreview)
                ? m_animationPreviewW : m_surface->w;
            const int shownH = (!m_animationFiles.empty() && m_animationPreview)
                ? m_animationPreviewH : m_surface->h;
            const ImVec2 size(
                std::max(1.0f, shownW * m_previewScale),
                std::max(1.0f, shownH * m_previewScale));

            const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
            if (m_onionSkinEnabled && m_onionSkinPreview && !m_animationFiles.empty())
            {
                const ImVec2 previousSize(
                    std::max(1.0f, m_onionSkinPreviewW * m_previewScale),
                    std::max(1.0f, m_onionSkinPreviewH * m_previewScale));
                ImGui::SetCursorScreenPos(ImVec2(
                    imageOrigin.x + m_onionSkinOffsetX * m_previewScale,
                    imageOrigin.y + m_onionSkinOffsetY * m_previewScale));
                ImGui::Image(reinterpret_cast<ImTextureID>(m_onionSkinPreview),
                    previousSize, ImVec2(0, 0), ImVec2(1, 1),
                    ImVec4(1.0f, 1.0f, 1.0f, m_onionSkinOpacity),
                    ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            }

            ImGui::SetCursorScreenPos(imageOrigin);
            ImGui::Image(reinterpret_cast<ImTextureID>(shownTexture), size);
            if (m_onionSkinEnabled && !m_animationFiles.empty())
            {
                if (ImGui::IsItemHovered() && ImGui::GetIO().KeyAlt &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    const ImVec2 delta = ImGui::GetIO().MouseDelta;
                    m_onionSkinOffsetX += static_cast<int>(std::lround(
                        delta.x / std::max(0.01f, m_previewScale)));
                    m_onionSkinOffsetY += static_cast<int>(std::lround(
                        delta.y / std::max(0.01f, m_previewScale)));
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Previous frame overlay | Alt + drag to align");
            }
        }
        else
        {
            ImGui::TextDisabled("Select a PNG asset.");
        }
        ImGui::EndChild();

        drawHorizontalSplitter("preview_operations_splitter", splitterThickness,
            m_operationsPanelHeight, minOperationsHeight, minPreviewHeight);

        ImGui::BeginChild("operations", ImVec2(0.0f, m_operationsPanelHeight), true);
        ImGui::TextUnformatted("Operations");
        ImGui::Separator();

        if (ImGui::Button("Trim transparent border")) trimTransparent();
        ImGui::SameLine();
        if (ImGui::Button("Dilate RGB edge 1 px")) dilateTransparentRgb(1);
        ImGui::SameLine();
        if (ImGui::Button("Dilate RGB edge 2 px")) dilateTransparentRgb(2);
        ImGui::SameLine();
        if (ImGui::Button("Flip horizontal")) flipHorizontal();

        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputInt("Offset X", &m_offsetX);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputInt("Offset Y", &m_offsetY);
        ImGui::SameLine();
        if (ImGui::Button("Apply offset")) applyOffset();

        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputInt("Resize %", &m_resizePercent);
        ImGui::SameLine();
        if (ImGui::Button("Resize nearest")) resizeNearest();
        ImGui::SameLine();
        if (ImGui::Button("Reload from disk")) reloadCurrent();

        ImGui::Spacing();
        if (ImGui::Button("Save PNG", ImVec2(150.0f, 0.0f))) saveCurrent();
        ImGui::SameLine();
        if (ImGui::Button("Save + reload map", ImVec2(170.0f, 0.0f)))
        {
            if (saveCurrent())
                m_reloadRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Align current animation group")) alignCurrentAnimationGroup();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Animation");

        if (m_animationFiles.empty())
        {
            ImGui::TextDisabled("No *_anim_XX.png frames found for this asset.");
            if (ImGui::Button("Create animation from current sprite"))
                createAnimationFromCurrent();
        }
        else
        {
            if (ImGui::Button(m_animationPlaying ? "Pause" : "Play"))
                m_animationPlaying = !m_animationPlaying;
            ImGui::SameLine();
            if (ImGui::Button("Previous frame")) stepAnimation(-1);
            ImGui::SameLine();
            if (ImGui::Button("Next frame")) stepAnimation(1);
            ImGui::SameLine();
            ImGui::Checkbox("Loop", &m_animationLoop);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderFloat("FPS", &m_animationFps, 1.0f, 30.0f, "%.1f");

            ImGui::Checkbox("Show previous frame", &m_onionSkinEnabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);
            ImGui::SliderFloat("Previous opacity", &m_onionSkinOpacity,
                0.05f, 0.95f, "%.2f", ImGuiSliderFlags_None);
            m_onionSkinOpacity = std::clamp(m_onionSkinOpacity, 0.05f, 0.95f);

            ImGui::SetNextItemWidth(110.0f);
            ImGui::InputInt("Overlay X", &m_onionSkinOffsetX);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::InputInt("Overlay Y", &m_onionSkinOffsetY);
            ImGui::SameLine();
            if (ImGui::Button("Reset overlay"))
            {
                m_onionSkinOffsetX = 0;
                m_onionSkinOffsetY = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("<")) --m_onionSkinOffsetX;
            ImGui::SameLine();
            if (ImGui::Button(">")) ++m_onionSkinOffsetX;
            ImGui::SameLine();
            if (ImGui::Button("Up")) --m_onionSkinOffsetY;
            ImGui::SameLine();
            if (ImGui::Button("Down")) ++m_onionSkinOffsetY;

            int frame = m_animationIndex;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderInt("Frame", &frame, 0,
                    static_cast<int>(m_animationFiles.size()) - 1))
            {
                m_animationIndex = frame;
                m_animationPlaying = false;
                rebuildAnimationPreview();
            }

            ImGui::Text("Frame %d / %d: %s",
                m_animationIndex + 1, static_cast<int>(m_animationFiles.size()),
                m_animationFiles[static_cast<std::size_t>(m_animationIndex)]
                    .filename().string().c_str());

            if (ImGui::Button("Duplicate selected frame"))
                duplicateAnimationFrame();
            ImGui::SameLine();
            if (ImGui::Button("Delete selected frame"))
                deleteAnimationFrame();
            ImGui::SameLine();
            if (ImGui::Button("Load frame for editing"))
                loadAnimationFrameIntoEditor();

            if (m_onionSkinOffsetX != 0 || m_onionSkinOffsetY != 0)
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
                    "Overlay offset is preview-only until it is baked into the frame.");
                if (ImGui::Button("Bake alignment into selected frame", ImVec2(280.0f, 0.0f)))
                    bakeOnionSkinAlignmentIntoSelectedFrame();
                ImGui::SameLine();
                ImGui::TextDisabled("Saves current frame at X=%d, Y=%d",
                    -m_onionSkinOffsetX, -m_onionSkinOffsetY);
            }
        }

        if (!m_status.empty())
        {
            ImGui::Separator();
            ImGui::TextWrapped("%s", m_status.c_str());
        }
        ImGui::EndChild();
        ImGui::EndGroup();
        ImGui::End();
    }

private:
    static void drawVerticalSplitter(const char* id, float thickness,
                                     float& leftWidth,
                                     float minLeft, float minRight)
    {
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float height = std::max(1.0f, ImGui::GetContentRegionAvail().y);
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Separator));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
        const std::string buttonId = std::string("##") + id;
        ImGui::Button(buttonId.c_str(), ImVec2(thickness, height));
        ImGui::PopStyleColor(3);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (hovered || active)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (active)
        {
            leftWidth += ImGui::GetIO().MouseDelta.x;
            const float available = ImGui::GetWindowContentRegionMax().x -
                                    ImGui::GetWindowContentRegionMin().x;
            leftWidth = std::clamp(leftWidth, minLeft,
                std::max(minLeft, available - minRight - thickness));
        }
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(cursor.x + 1.0f, cursor.y + 8.0f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), "||");
    }

    static void drawHorizontalSplitter(const char* id, float thickness,
                                       float& bottomHeight,
                                       float minBottom, float minTop)
    {
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Separator));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
        const std::string buttonId = std::string("##") + id;
        ImGui::Button(buttonId.c_str(), ImVec2(width, thickness));
        ImGui::PopStyleColor(3);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (hovered || active)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (active)
        {
            bottomHeight -= ImGui::GetIO().MouseDelta.y;
            const float available = ImGui::GetWindowContentRegionMax().y -
                                    ImGui::GetWindowContentRegionMin().y;
            bottomHeight = std::clamp(bottomHeight, minBottom,
                std::max(minBottom, available - minTop - thickness));
        }
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(cursor.x + width * 0.5f - 8.0f, cursor.y - 2.0f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), "====");
    }

    static bool containsNoCase(std::string text, std::string token)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(token.begin(), token.end(), token.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return token.empty() || text.find(token) != std::string::npos;
    }

    static std::uint32_t* row(SDL_Surface* surface, int y)
    {
        return reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(surface->pixels) + y * surface->pitch);
    }

    void refreshFileList()
    {
        m_files.clear();
        std::error_code ec;
        if (!std::filesystem::exists(m_assetDir, ec))
        {
            m_status = "Asset folder not found: " + m_assetDir.string();
            return;
        }
        for (const auto& entry : std::filesystem::directory_iterator(m_assetDir, ec))
        {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".png") m_files.push_back(entry.path());
        }
        std::sort(m_files.begin(), m_files.end());
        if (!m_files.empty() && m_selectedIndex < 0)
            loadIndex(0);
        else
            rebuildAnimationGroup();
    }

    void loadIndex(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_files.size())) return;
        m_selectedIndex = index;
        m_currentPath = m_files[static_cast<std::size_t>(index)];
        reloadCurrent();
    }

    void reloadCurrent()
    {
        if (m_currentPath.empty()) return;
        SDL_Surface* loaded = IMG_Load(m_currentPath.string().c_str());
        if (!loaded)
        {
            m_status = "IMG_Load failed: " + m_currentPath.string();
            return;
        }
        SDL_Surface* converted = SDL_ConvertSurfaceFormat(
            loaded, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(loaded);
        if (!converted)
        {
            m_status = "Surface conversion failed.";
            return;
        }
        freeSurface();
        m_surface = converted;
        m_dirty = false;
        rebuildPreview();
        rebuildAnimationGroup();
        m_status = "Loaded " + m_currentPath.filename().string();
    }

    bool saveCurrent()
    {
        if (!m_surface || m_currentPath.empty()) return false;
        if (IMG_SavePNG(m_surface, m_currentPath.string().c_str()) != 0)
        {
            m_status = "IMG_SavePNG failed: " + std::string(IMG_GetError());
            return false;
        }
        m_dirty = false;
        m_status = "Saved " + m_currentPath.filename().string();
        if (m_autoReload) m_reloadRequested = true;
        return true;
    }

    void rebuildPreview()
    {
        destroyPreview();
        if (!m_surface || !m_renderer) return;
        m_preview = SDL_CreateTextureFromSurface(m_renderer, m_surface);
        if (m_preview)
        {
            SDL_SetTextureBlendMode(m_preview, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
            SDL_SetTextureScaleMode(m_preview, SDL_ScaleModeNearest);
#endif
        }
    }

    void replaceSurface(SDL_Surface* replacement)
    {
        if (!replacement) return;
        freeSurface();
        m_surface = replacement;
        m_dirty = true;
        rebuildPreview();
    }

    void trimTransparent()
    {
        if (!m_surface) return;
        int minX = m_surface->w, minY = m_surface->h, maxX = -1, maxY = -1;
        SDL_LockSurface(m_surface);
        for (int y = 0; y < m_surface->h; ++y)
        {
            const auto* pixels = row(m_surface, y);
            for (int x = 0; x < m_surface->w; ++x)
            {
                if ((pixels[x] >> 24) == 0) continue;
                minX = std::min(minX, x); minY = std::min(minY, y);
                maxX = std::max(maxX, x); maxY = std::max(maxY, y);
            }
        }
        SDL_UnlockSurface(m_surface);
        if (maxX < minX || maxY < minY) return;
        const int w = maxX - minX + 1, h = maxY - minY + 1;
        SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Rect src{minX, minY, w, h};
        SDL_BlitSurface(m_surface, &src, out, nullptr);
        replaceSurface(out);
    }

    void dilateTransparentRgb(int radius)
    {
        if (!m_surface) return;
        SDL_Surface* out = SDL_ConvertSurface(m_surface, m_surface->format, 0);
        if (!out) return;
        SDL_LockSurface(m_surface); SDL_LockSurface(out);
        for (int y = 0; y < m_surface->h; ++y)
        {
            auto* dst = row(out, y);
            const auto* srcRow = row(m_surface, y);
            for (int x = 0; x < m_surface->w; ++x)
            {
                if ((srcRow[x] >> 24) != 0) continue;
                bool found = false;
                std::uint32_t rgb = 0;
                for (int r = 1; r <= radius && !found; ++r)
                {
                    for (int yy = std::max(0, y-r); yy <= std::min(m_surface->h-1, y+r) && !found; ++yy)
                    {
                        const auto* src = row(m_surface, yy);
                        for (int xx = std::max(0, x-r); xx <= std::min(m_surface->w-1, x+r); ++xx)
                        {
                            if ((src[xx] >> 24) == 0) continue;
                            rgb = src[xx] & 0x00ffffffu; found = true; break;
                        }
                    }
                }
                if (found) dst[x] = rgb; // preserve alpha 0, replace hidden RGB fringe
            }
        }
        SDL_UnlockSurface(out); SDL_UnlockSurface(m_surface);
        replaceSurface(out);
    }

    void flipHorizontal()
    {
        if (!m_surface) return;
        SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, m_surface->w, m_surface->h, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_LockSurface(m_surface); SDL_LockSurface(out);
        for (int y = 0; y < m_surface->h; ++y)
        {
            const auto* src = row(m_surface, y); auto* dst = row(out, y);
            for (int x = 0; x < m_surface->w; ++x) dst[m_surface->w - 1 - x] = src[x];
        }
        SDL_UnlockSurface(out); SDL_UnlockSurface(m_surface);
        replaceSurface(out);
    }

    void applyOffset()
    {
        if (!m_surface || (m_offsetX == 0 && m_offsetY == 0)) return;
        SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, m_surface->w, m_surface->h, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_FillRect(out, nullptr, 0x00000000u);
        SDL_Rect dst{m_offsetX, m_offsetY, m_surface->w, m_surface->h};
        SDL_BlitSurface(m_surface, nullptr, out, &dst);
        replaceSurface(out);
    }

    void resizeNearest()
    {
        if (!m_surface) return;
        m_resizePercent = std::clamp(m_resizePercent, 10, 800);
        const int newW = std::max(1, m_surface->w * m_resizePercent / 100);
        const int newH = std::max(1, m_surface->h * m_resizePercent / 100);
        SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, newW, newH, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_LockSurface(m_surface); SDL_LockSurface(out);
        for (int y = 0; y < newH; ++y)
        {
            auto* dst = row(out, y);
            const int sy = std::min(m_surface->h - 1, y * m_surface->h / newH);
            const auto* src = row(m_surface, sy);
            for (int x = 0; x < newW; ++x)
            {
                const int sx = std::min(m_surface->w - 1, x * m_surface->w / newW);
                dst[x] = src[sx];
            }
        }
        SDL_UnlockSurface(out); SDL_UnlockSurface(m_surface);
        replaceSurface(out);
    }

    void alignCurrentAnimationGroup()
    {
        // The internal editor intentionally keeps this conservative: it aligns
        // all existing <stem>_anim_XX.png frames to the same canvas center.
        if (m_currentPath.empty()) return;
        std::string stem = m_currentPath.stem().string();
        const std::size_t pos = stem.find("_anim_");
        if (pos != std::string::npos) stem = stem.substr(0, pos);

        std::vector<std::filesystem::path> group;
        for (const auto& p : m_files)
        {
            const std::string s = p.stem().string();
            if (s.rfind(stem + "_anim_", 0) == 0) group.push_back(p);
        }
        if (group.empty())
        {
            m_status = "No animation frames found for: " + stem;
            return;
        }

        int maxW = 0, maxH = 0, saved = 0;
        std::vector<SDL_Surface*> frames;
        for (const auto& p : group)
        {
            SDL_Surface* loaded = IMG_Load(p.string().c_str());
            SDL_Surface* converted = loaded ? SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0) : nullptr;
            if (loaded) SDL_FreeSurface(loaded);
            if (!converted) continue;
            maxW = std::max(maxW, converted->w); maxH = std::max(maxH, converted->h);
            frames.push_back(converted);
        }
        for (std::size_t i = 0; i < frames.size(); ++i)
        {
            SDL_Surface* frame = frames[i];
            SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, maxW, maxH, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_FillRect(out, nullptr, 0x00000000u);
            SDL_Rect dst{(maxW-frame->w)/2, (maxH-frame->h)/2, frame->w, frame->h};
            SDL_BlitSurface(frame, nullptr, out, &dst);
            IMG_SavePNG(out, group[i].string().c_str());
            SDL_FreeSurface(out); SDL_FreeSurface(frame); ++saved;
        }
        m_status = "Centered " + std::to_string(saved) + " frame(s) for " + stem;
        reloadCurrent();
        if (m_autoReload) m_reloadRequested = true;
    }

    static std::string animationStem(const std::filesystem::path& path)
    {
        std::string stem = path.stem().string();
        const std::size_t pos = stem.find("_anim_");
        if (pos != std::string::npos)
            stem.resize(pos);
        return stem;
    }

    void rebuildAnimationGroup()
    {
        m_animationFiles.clear();
        destroyAnimationPreview();
        if (m_currentPath.empty())
            return;

        const std::string stem = animationStem(m_currentPath);
        for (const auto& path : m_files)
        {
            const std::string candidate = path.stem().string();
            if (candidate.rfind(stem + "_anim_", 0) == 0)
                m_animationFiles.push_back(path);
        }
        std::sort(m_animationFiles.begin(), m_animationFiles.end());
        if (m_animationFiles.empty())
        {
            m_animationIndex = 0;
            m_animationPlaying = false;
            return;
        }
        m_animationIndex = std::clamp(
            m_animationIndex, 0,
            static_cast<int>(m_animationFiles.size()) - 1);
        rebuildAnimationPreview();
    }

    void rebuildAnimationPreview()
    {
        destroyAnimationPreview();
        destroyOnionSkinPreview();
        if (!m_renderer || m_animationFiles.empty())
            return;
        m_animationIndex = std::clamp(
            m_animationIndex, 0,
            static_cast<int>(m_animationFiles.size()) - 1);
        SDL_Surface* frame = IMG_Load(
            m_animationFiles[static_cast<std::size_t>(m_animationIndex)]
                .string().c_str());
        if (!frame)
            return;
        m_animationPreviewW = frame->w;
        m_animationPreviewH = frame->h;
        m_animationPreview = SDL_CreateTextureFromSurface(m_renderer, frame);
        SDL_FreeSurface(frame);
        if (m_animationPreview)
        {
            SDL_SetTextureBlendMode(m_animationPreview, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
            SDL_SetTextureScaleMode(m_animationPreview, SDL_ScaleModeNearest);
#endif
        }

        const int count = static_cast<int>(m_animationFiles.size());
        if (count > 1)
        {
            int previousIndex = m_animationIndex - 1;
            if (previousIndex < 0)
                previousIndex = m_animationLoop ? count - 1 : 0;
            SDL_Surface* previous = IMG_Load(
                m_animationFiles[static_cast<std::size_t>(previousIndex)]
                    .string().c_str());
            if (previous)
            {
                m_onionSkinPreviewW = previous->w;
                m_onionSkinPreviewH = previous->h;
                m_onionSkinPreview = SDL_CreateTextureFromSurface(m_renderer, previous);
                SDL_FreeSurface(previous);
                if (m_onionSkinPreview)
                {
                    SDL_SetTextureBlendMode(m_onionSkinPreview, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
                    SDL_SetTextureScaleMode(m_onionSkinPreview, SDL_ScaleModeNearest);
#endif
                }
            }
        }
    }

    void destroyOnionSkinPreview()
    {
        if (m_onionSkinPreview)
            SDL_DestroyTexture(m_onionSkinPreview);
        m_onionSkinPreview = nullptr;
        m_onionSkinPreviewW = 0;
        m_onionSkinPreviewH = 0;
    }

    void destroyAnimationPreview()
    {
        if (m_animationPreview)
            SDL_DestroyTexture(m_animationPreview);
        m_animationPreview = nullptr;
        m_animationPreviewW = 0;
        m_animationPreviewH = 0;
    }

    void stepAnimation(int direction)
    {
        if (m_animationFiles.empty()) return;
        const int count = static_cast<int>(m_animationFiles.size());
        m_animationIndex = (m_animationIndex + direction + count) % count;
        m_animationPlaying = false;
        m_animationAccumulator = 0.0f;
        rebuildAnimationPreview();
    }

    void createAnimationFromCurrent()
    {
        if (!m_surface || m_currentPath.empty()) return;
        const std::string stem = animationStem(m_currentPath);
        const std::filesystem::path output =
            m_currentPath.parent_path() / (stem + "_anim_00.png");
        if (IMG_SavePNG(m_surface, output.string().c_str()) != 0)
        {
            m_status = "Cannot create animation frame: " +
                std::string(IMG_GetError());
            return;
        }
        refreshFileList();
        m_status = "Created " + output.filename().string();
    }

    std::filesystem::path nextAnimationFramePath() const
    {
        if (m_currentPath.empty()) return {};
        const std::string stem = animationStem(m_currentPath);
        int index = 0;
        for (;; ++index)
        {
            char suffix[32]{};
            std::snprintf(suffix, sizeof(suffix), "_anim_%02d.png", index);
            const auto candidate = m_currentPath.parent_path() / (stem + suffix);
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec))
                return candidate;
        }
    }

    void bakeOnionSkinAlignmentIntoSelectedFrame()
    {
        if (m_animationFiles.empty())
        {
            m_status = "No animation frame selected.";
            return;
        }
        if (m_onionSkinOffsetX == 0 && m_onionSkinOffsetY == 0)
        {
            m_status = "Overlay is already aligned; nothing to bake.";
            return;
        }

        const std::filesystem::path target =
            m_animationFiles[static_cast<std::size_t>(m_animationIndex)];
        SDL_Surface* loaded = IMG_Load(target.string().c_str());
        if (!loaded)
        {
            m_status = "Cannot load animation frame: " + target.filename().string();
            return;
        }
        SDL_Surface* frame = SDL_ConvertSurfaceFormat(
            loaded, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(loaded);
        if (!frame)
        {
            m_status = "Cannot convert animation frame: " + target.filename().string();
            return;
        }

        SDL_Surface* shifted = SDL_CreateRGBSurfaceWithFormat(
            0, frame->w, frame->h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!shifted)
        {
            SDL_FreeSurface(frame);
            m_status = "Cannot allocate aligned animation frame.";
            return;
        }
        SDL_FillRect(shifted, nullptr, 0x00000000u);

        // The onion skin moves the previous frame relative to the current one.
        // Persisting the same visual alignment therefore moves the selected
        // current frame in the opposite direction.
        SDL_Rect destination{
            -m_onionSkinOffsetX,
            -m_onionSkinOffsetY,
            frame->w,
            frame->h
        };
        SDL_BlitSurface(frame, nullptr, shifted, &destination);
        SDL_FreeSurface(frame);

        if (IMG_SavePNG(shifted, target.string().c_str()) != 0)
        {
            m_status = "Cannot save aligned frame: " +
                std::string(IMG_GetError());
            SDL_FreeSurface(shifted);
            return;
        }
        SDL_FreeSurface(shifted);

        const int savedX = -m_onionSkinOffsetX;
        const int savedY = -m_onionSkinOffsetY;
        m_onionSkinOffsetX = 0;
        m_onionSkinOffsetY = 0;
        m_animationPlaying = false;
        m_animationAccumulator = 0.0f;

        if (m_currentPath == target)
            loadIndex(m_selectedIndex);
        else
            rebuildAnimationPreview();

        m_status = "Saved aligned position for " + target.filename().string() +
            " (X=" + std::to_string(savedX) +
            ", Y=" + std::to_string(savedY) + ").";
        if (m_autoReload)
            m_reloadRequested = true;
    }

    void duplicateAnimationFrame()
    {
        if (m_animationFiles.empty()) return;
        const auto output = nextAnimationFramePath();
        std::error_code ec;
        std::filesystem::copy_file(
            m_animationFiles[static_cast<std::size_t>(m_animationIndex)],
            output, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            m_status = "Frame duplication failed: " + ec.message();
            return;
        }
        refreshFileList();
        m_status = "Created " + output.filename().string();
    }

    void deleteAnimationFrame()
    {
        if (m_animationFiles.empty()) return;
        const auto deleted =
            m_animationFiles[static_cast<std::size_t>(m_animationIndex)];
        std::error_code ec;
        std::filesystem::remove(deleted, ec);
        if (ec)
        {
            m_status = "Frame deletion failed: " + ec.message();
            return;
        }
        m_animationIndex = std::max(0, m_animationIndex - 1);
        refreshFileList();
        m_status = "Deleted " + deleted.filename().string();
    }

    void loadAnimationFrameIntoEditor()
    {
        if (m_animationFiles.empty()) return;
        const auto target =
            m_animationFiles[static_cast<std::size_t>(m_animationIndex)];
        auto it = std::find(m_files.begin(), m_files.end(), target);
        if (it == m_files.end()) return;
        loadIndex(static_cast<int>(std::distance(m_files.begin(), it)));
        m_animationPlaying = false;
    }

    void destroyPreview() { if (m_preview) SDL_DestroyTexture(m_preview); m_preview = nullptr; }
    void freeSurface() { if (m_surface) SDL_FreeSurface(m_surface); m_surface = nullptr; }

private:
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture* m_preview = nullptr;
    SDL_Texture* m_animationPreview = nullptr;
    SDL_Texture* m_onionSkinPreview = nullptr;
    SDL_Surface* m_surface = nullptr;
    std::filesystem::path m_assetDir;
    std::filesystem::path m_currentPath;
    std::vector<std::filesystem::path> m_files;
    std::vector<std::filesystem::path> m_animationFiles;
    int m_selectedIndex = -1;
    int m_animationIndex = 0;
    int m_animationPreviewW = 0;
    int m_animationPreviewH = 0;
    int m_onionSkinPreviewW = 0;
    int m_onionSkinPreviewH = 0;
    bool m_open = false;
    bool m_dirty = false;
    bool m_autoReload = true;
    bool m_reloadRequested = false;
    bool m_animationPlaying = false;
    bool m_animationLoop = true;
    bool m_onionSkinEnabled = true;
    float m_onionSkinOpacity = 0.28f;
    int m_onionSkinOffsetX = 0;
    int m_onionSkinOffsetY = 0;
    float m_animationFps = 8.0f;
    float m_animationAccumulator = 0.0f;
    float m_previewScale = 1.0f;
    int m_offsetX = 0;
    int m_offsetY = 0;
    int m_resizePercent = 100;
    char m_filter[128]{};

    float m_assetPanelWidth = 270.0f;
    float m_operationsPanelHeight = 220.0f;
    std::string m_status;
};
