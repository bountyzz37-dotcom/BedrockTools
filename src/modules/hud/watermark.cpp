#include "watermark.hpp"
#include "modules/ModuleRegistry.hpp"
#include "core/memory/Hooks.hpp"

#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <list>
#include <string>
#include <vector>

namespace {

// Same character-width heuristic infooverlay.cpp / pingcounter.cpp use for laying out
// variable-width text without a real text-measurement API.
float calcTextWidth(const std::string& text, float size) {
    float width = 0;
    for (char c : text) {
        if (c == 'i' || c == 'l' || c == '1' || c == ':' || c == '.' || c == ' ') width += size * 0.3f;
        else if (c == 'm' || c == 'w' || c == 'M' || c == 'W') width += size * 0.8f;
        else width += size * 0.58f;
    }
    return width;
}

WatermarkModule* g_watermarkMod = nullptr;
void (*_raknetUpdate_orig)(void*) = nullptr;

void _raknetUpdate_hook(void* _this) {
    if (_raknetUpdate_orig) _raknetUpdate_orig(_this);
    if (g_watermarkMod && g_watermarkMod->enabled) {
        int avgPing = *(int*)((uintptr_t)_this + bedrocktools::sdk::offsets::RakNetConnector::mAvgPing);
        if (avgPing >= 0) g_watermarkMod->m_ping = avgPing;
    }
}

// signal-quality tier purely derived from our own ping reading (there's no OS radio/wifi
// signal API reachable from inside the game process) -- 4 bars = <=50ms down to 1 bar = >150ms.
int signalTierFromPing(int pingMs) {
    if (pingMs <= 50)  return 4;
    if (pingMs <= 100) return 3;
    if (pingMs <= 150) return 2;
    return 1;
}

// -- icon builders: everything below is drawn from primitives already used elsewhere in this
// project (RectFilled/CircleFilled/TriangleFilled/Line), since no logo/glyph texture assets
// or icon-atlas offsets exist anywhere in this codebase to draw from instead.

void drawEyeLogo(std::vector<PLModMenu_DrawCommand>& cmds, float cx, float cy, float size,
                  uint32_t irisColor, uint32_t pupilColor) {
    PLModMenu_DrawCommand iris = {};
    iris.type = PL_DRAW_CIRCLE_FILLED;
    iris.x = cx; iris.y = cy;
    iris.size = size / 2.f;
    iris.color = irisColor;
    cmds.push_back(iris);

    PLModMenu_DrawCommand pupil = {};
    pupil.type = PL_DRAW_CIRCLE_FILLED;
    pupil.x = cx; pupil.y = cy;
    pupil.size = size / 2.f * 0.42f;
    pupil.color = pupilColor;
    cmds.push_back(pupil);
}

// Two overlapping triangles approximating a bolt/"Z" zigzag -- drawn inside the box
// [bx,by]..[bx+s,by+s].
void drawBoltIcon(std::vector<PLModMenu_DrawCommand>& cmds, float bx, float by, float s, uint32_t color) {
    PLModMenu_DrawCommand top = {};
    top.type = PL_DRAW_TRIANGLE_FILLED;
    top.x  = bx + s * 0.58f; top.y  = by;
    top.w  = bx + s * 0.08f; top.h  = by + s * 0.60f;
    top.x3 = bx + s * 0.55f; top.y3 = by + s * 0.60f;
    top.color = color;
    cmds.push_back(top);

    PLModMenu_DrawCommand bottom = {};
    bottom.type = PL_DRAW_TRIANGLE_FILLED;
    bottom.x  = bx + s * 0.45f; bottom.y  = by + s * 0.40f;
    bottom.w  = bx + s * 0.95f; bottom.h  = by + s * 0.40f;
    bottom.x3 = bx + s * 0.42f; bottom.y3 = by + s;
    bottom.color = color;
    cmds.push_back(bottom);
}

// 4 ascending bars inside the box [bx,by]..[bx+s,by+s]; `litBars` of them drawn at full color,
// the rest dimmed -- litBars comes from signalTierFromPing().
void drawSignalBars(std::vector<PLModMenu_DrawCommand>& cmds, float bx, float by, float s,
                     int litBars, uint32_t color, float dimOpacity) {
    const int kBars = 4;
    const float barW = s * 0.16f;
    const float gap = s * 0.10f;
    const float totalW = kBars * barW + (kBars - 1) * gap;
    float x = bx + (s - totalW) / 2.f;

    uint32_t dimColor = ((uint32_t)(dimOpacity * 255.f) << 24) | (color & 0x00FFFFFF);

    for (int i = 0; i < kBars; ++i) {
        float h = s * (0.30f + 0.23f * i);
        PLModMenu_DrawCommand bar = {};
        bar.type = PL_DRAW_RECT_FILLED;
        bar.x = x;
        bar.y = by + s - h;
        bar.w = barW;
        bar.h = h;
        bar.x3 = barW * 0.3f;
        bar.color = (i < litBars) ? color : dimColor;
        cmds.push_back(bar);
        x += barW + gap;
    }
}

} // namespace

WatermarkModule::WatermarkModule()
    : Module("Watermark", "Shows a logo / FPS / ping / clock capsule watermark on screen.") {
    g_watermarkMod = this;
}

WatermarkModule::~WatermarkModule() {
    if (g_watermarkMod == this) g_watermarkMod = nullptr;
}

void WatermarkModule::onInit() {
    if (m_patchTarget) return;
    uintptr_t addr = bedrocktools::memory::resolve(bedrocktools::memory::SignatureId::RaknetUpdate);
    if (addr != 0) m_patchTarget = (void*)addr;
}

void WatermarkModule::applyPatch() {
    if (m_patched || !m_patchTarget) return;
    bedrocktools::hooks::install(m_patchTarget, (void*)_raknetUpdate_hook, (void**)&_raknetUpdate_orig);
    m_patched = true;
}

void WatermarkModule::onEnable() {
    applyPatch();
    m_hasLastFrame = false;
    m_accumSeconds = 0.f;
    m_accumFrameCount = 0;
}

void WatermarkModule::onDisable() {}

void WatermarkModule::onFrame() {
    if (!enabled) return;

    // ---- 1) FPS purely from render-frame timing, identical technique to InfoOverlayModule ----
    auto now = std::chrono::steady_clock::now();
    if (m_hasLastFrame) {
        float dt = std::chrono::duration<float>(now - m_lastFrameTime).count();
        if (dt > 0.f && dt < 5.f) {
            m_accumSeconds += dt;
            m_accumFrameCount++;
            float interval = m_fpsUpdateSpeed > 0.01f ? m_fpsUpdateSpeed : 0.5f;
            if (m_accumSeconds >= interval && m_accumFrameCount > 0) {
                m_displayFps = (int)std::lround(m_accumFrameCount / m_accumSeconds);
                m_displayFrameMs = (m_accumSeconds / m_accumFrameCount) * 1000.f;
                m_accumSeconds = 0.f;
                m_accumFrameCount = 0;
            }
        }
    }
    m_lastFrameTime = now;
    m_hasLastFrame = true;

    // ---- 2) 24-hour wall-clock time (device time, not Minecraft's in-game day cycle) ----
    std::time_t nowTimeT = std::time(nullptr);
    std::tm localTm{};
    localtime_r(&nowTimeT, &localTm);
    char clockBuf[8];
    std::snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d", localTm.tm_hour, localTm.tm_min);

    // ---- 3) build segments (icon-box width reserved inline where a segment has its own icon) ----
    enum class SegIcon { None, Bolt, Bars };
    struct Segment { std::string text; SegIcon icon; };
    std::vector<Segment> segments;
    if (m_showFps)   segments.push_back({std::to_string(m_displayFps) + " fps", SegIcon::Bolt});
    if (m_showPing)  segments.push_back({std::to_string(m_ping) + " ms", SegIcon::Bars});
    if (m_showClock) segments.push_back({clockBuf, SegIcon::None});

    const float dividerHeight = m_textSize * 0.9f;
    const float pillH = std::max({m_logoSize, m_iconSize, m_textSize + 8.f}) + m_paddingY * 2.f;

    // ---- 4) measure content width so the pill hugs its contents ----
    float contentWidth = 0.f;
    if (m_showLogo) contentWidth += m_logoSize + m_itemGap;

    std::vector<float> segWidths;
    segWidths.reserve(segments.size());
    for (size_t i = 0; i < segments.size(); ++i) {
        float w = calcTextWidth(segments[i].text, m_textSize);
        if (segments[i].icon != SegIcon::None) w += m_iconSize + m_itemGap * 0.6f;
        segWidths.push_back(w);
        contentWidth += w;
        if (i + 1 < segments.size()) contentWidth += m_itemGap + 1.f + m_itemGap;
    }
    if (m_showSignal) contentWidth += m_itemGap + 1.f + m_itemGap + m_iconSize;

    if (contentWidth <= 0.f) return; // everything toggled off

    const float pillW = contentWidth + m_paddingX * 2.f;

    std::vector<PLModMenu_DrawCommand> cmds;
    std::list<std::string> stringStore; // keep .c_str() pointers alive until submitDrawCommands

    // ---- 5) capsule background (stadium shape) ----
    {
        PLModMenu_DrawCommand bg = {};
        bg.type = PL_DRAW_RECT_FILLED;
        bg.x = hudPosX; bg.y = hudPosY;
        bg.w = pillW; bg.h = pillH;
        bg.x3 = pillH / 2.f;
        int alpha = (int)(m_backgroundOpacity * 255.f);
        bg.color = ((uint32_t)alpha << 24) | (m_backgroundColorHex & 0x00FFFFFF);
        cmds.push_back(bg);
    }

    float cursorX = hudPosX + m_paddingX;
    const float centerY = hudPosY + pillH / 2.f;

    auto drawDivider = [&]() {
        cursorX += m_itemGap;
        PLModMenu_DrawCommand line = {};
        line.type = PL_DRAW_LINE;
        line.x = cursorX; line.y = centerY - dividerHeight / 2.f;
        line.w = 0.f; line.h = dividerHeight;
        line.size = 1.2f;
        int alpha = (int)(m_dividerOpacity * 255.f);
        line.color = ((uint32_t)alpha << 24) | (m_dividerColorHex & 0x00FFFFFF);
        cmds.push_back(line);
        cursorX += 1.f + m_itemGap;
    };

    // ---- 6) logo ----
    if (m_showLogo) {
        drawEyeLogo(cmds, cursorX + m_logoSize / 2.f, centerY, m_logoSize, m_accentColorHex, m_logoPupilColorHex);
        cursorX += m_logoSize + m_itemGap;
    }

    // ---- 7) fps / ping / clock segments, each with its own icon where noted ----
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i > 0 || m_showLogo) drawDivider();

        const auto& seg = segments[i];
        if (seg.icon != SegIcon::None) {
            float iconY = centerY - m_iconSize / 2.f;
            if (seg.icon == SegIcon::Bolt) {
                drawBoltIcon(cmds, cursorX, iconY, m_iconSize, m_accentColorHex);
            } else {
                int tier = signalTierFromPing(m_ping);
                drawSignalBars(cmds, cursorX, iconY, m_iconSize, tier, m_signalColorHex, m_signalDimOpacity);
            }
            cursorX += m_iconSize + m_itemGap * 0.6f;
        }

        stringStore.push_back(seg.text);
        PLModMenu_DrawCommand txt = {};
        txt.type = PL_DRAW_TEXT;
        txt.x = cursorX; txt.y = hudPosY;
        txt.w = calcTextWidth(seg.text, m_textSize);
        txt.h = pillH;
        txt.color = m_textColorHex;
        txt.size = m_textSize;
        txt.text = stringStore.back().c_str();
        cmds.push_back(txt);
        cursorX += txt.w;
    }

    // ---- 8) trailing standalone signal-bars glyph (matches the reference screenshot) ----
    if (m_showSignal) {
        drawDivider();
        int tier = signalTierFromPing(m_ping);
        drawSignalBars(cmds, cursorX, centerY - m_iconSize / 2.f, m_iconSize, tier, m_signalColorHex, m_signalDimOpacity);
    }

    submitDrawCommands(moduleId, cmds);
}

void WatermarkModule::loadConfig(const nlohmann::json& j) {
    Module::loadConfig(j);
    if (j.contains("hudPosX")) hudPosX = j["hudPosX"].get<float>();
    if (j.contains("hudPosY")) hudPosY = j["hudPosY"].get<float>();
    if (j.contains("isHudModule")) isHudModule = j["isHudModule"].get<bool>();

    if (j.contains("m_showLogo")) m_showLogo = j["m_showLogo"].get<bool>();
    if (j.contains("m_showFps")) m_showFps = j["m_showFps"].get<bool>();
    if (j.contains("m_showPing")) m_showPing = j["m_showPing"].get<bool>();
    if (j.contains("m_showClock")) m_showClock = j["m_showClock"].get<bool>();
    if (j.contains("m_showSignal")) m_showSignal = j["m_showSignal"].get<bool>();

    if (j.contains("m_textSize")) m_textSize = j["m_textSize"].get<float>();
    if (j.contains("m_iconSize")) m_iconSize = j["m_iconSize"].get<float>();
    if (j.contains("m_logoSize")) m_logoSize = j["m_logoSize"].get<float>();
    if (j.contains("m_paddingX")) m_paddingX = j["m_paddingX"].get<float>();
    if (j.contains("m_paddingY")) m_paddingY = j["m_paddingY"].get<float>();
    if (j.contains("m_itemGap")) m_itemGap = j["m_itemGap"].get<float>();
    if (j.contains("m_fpsUpdateSpeed")) m_fpsUpdateSpeed = j["m_fpsUpdateSpeed"].get<float>();

    if (j.contains("m_backgroundColorHex")) m_backgroundColorHex = j["m_backgroundColorHex"].get<uint32_t>();
    if (j.contains("m_backgroundOpacity")) m_backgroundOpacity = j["m_backgroundOpacity"].get<float>();
    if (j.contains("m_accentColorHex")) m_accentColorHex = j["m_accentColorHex"].get<uint32_t>();
    if (j.contains("m_logoPupilColorHex")) m_logoPupilColorHex = j["m_logoPupilColorHex"].get<uint32_t>();
    if (j.contains("m_textColorHex")) m_textColorHex = j["m_textColorHex"].get<uint32_t>();
    if (j.contains("m_dividerColorHex")) m_dividerColorHex = j["m_dividerColorHex"].get<uint32_t>();
    if (j.contains("m_dividerOpacity")) m_dividerOpacity = j["m_dividerOpacity"].get<float>();
    if (j.contains("m_signalColorHex")) m_signalColorHex = j["m_signalColorHex"].get<uint32_t>();
    if (j.contains("m_signalDimOpacity")) m_signalDimOpacity = j["m_signalDimOpacity"].get<float>();
}

void WatermarkModule::saveConfig(nlohmann::json& j) {
    Module::saveConfig(j);
    j["hudPosX"] = hudPosX;
    j["hudPosY"] = hudPosY;
    j["isHudModule"] = isHudModule;

    j["m_showLogo"] = m_showLogo;
    j["m_showFps"] = m_showFps;
    j["m_showPing"] = m_showPing;
    j["m_showClock"] = m_showClock;
    j["m_showSignal"] = m_showSignal;

    j["m_textSize"] = m_textSize;
    j["m_iconSize"] = m_iconSize;
    j["m_logoSize"] = m_logoSize;
    j["m_paddingX"] = m_paddingX;
    j["m_paddingY"] = m_paddingY;
    j["m_itemGap"] = m_itemGap;
    j["m_fpsUpdateSpeed"] = m_fpsUpdateSpeed;

    j["m_backgroundColorHex"] = m_backgroundColorHex;
    j["m_backgroundOpacity"] = m_backgroundOpacity;
    j["m_accentColorHex"] = m_accentColorHex;
    j["m_logoPupilColorHex"] = m_logoPupilColorHex;
    j["m_textColorHex"] = m_textColorHex;
    j["m_dividerColorHex"] = m_dividerColorHex;
    j["m_dividerOpacity"] = m_dividerOpacity;
    j["m_signalColorHex"] = m_signalColorHex;
    j["m_signalDimOpacity"] = m_signalDimOpacity;
}
