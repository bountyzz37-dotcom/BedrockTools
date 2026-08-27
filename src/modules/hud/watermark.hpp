#pragma once

#include "../Module.hpp"
#include <chrono>
#include <cstdint>

// "Watermark" -- capsule pill showing a logo badge, live FPS, ping, and the 24-hour clock,
// styled after the reference screenshot (eye logo | bolt+FPS | bars+ping | HH:MM | bars).
// Built the same way InfoOverlayModule/PingCounterModule already do it in this project:
//   - FPS: sampled purely from render-frame timing (see InfoOverlayModule::onFrame).
//   - Ping: SignatureId::RaknetUpdate hook + RakNetConnector::mAvgPing, same as PingCounterModule.
//   - Clock: real device wall-clock time (not Minecraft's day cycle), 24-hour HH:MM.
//   - Icons: no logo/bolt/signal textures exist anywhere in this project (only PL_DRAW_TEXT /
//     RECT_FILLED / LINE / CIRCLE_FILLED / TRIANGLE_FILLED / IMAGE-of-a-registered-buffer are
//     available -- see infooverlay.cpp's checkmark for precedent), so all three are built from
//     those primitives rather than being pixel-identical to the screenshot's artwork.
class WatermarkModule : public Module {
public:
    WatermarkModule();
    ~WatermarkModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onFrame() override;

    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

    void applyPatch();

    // -- HUD drag position (same convention as every other HUD-category module) --
    float hudPosX = 40.f;
    float hudPosY = 40.f;
    bool  isHudModule = true;

    // -- segment toggles --
    bool m_showLogo   = true;
    bool m_showFps    = true;
    bool m_showPing   = true;
    bool m_showClock  = true;
    bool m_showSignal = true;

    // -- layout --
    float m_textSize      = 15.f;
    float m_iconSize      = 14.f;
    float m_logoSize      = 20.f;
    float m_paddingX      = 14.f;
    float m_paddingY      = 9.f;
    float m_itemGap       = 10.f;
    float m_fpsUpdateSpeed= 0.5f;

    // -- colors (0xAARRGGBB packing, same convention as infooverlay/pingcounter) --
    uint32_t m_backgroundColorHex = 0xFF15141C;
    float    m_backgroundOpacity  = 0.85f;
    uint32_t m_accentColorHex     = 0xFF9B7CF6; // logo + bolt icon (violet, per screenshot)
    uint32_t m_logoPupilColorHex  = 0xFF15141C;
    uint32_t m_textColorHex       = 0xFFFFFFFF;
    uint32_t m_dividerColorHex    = 0xFFFFFFFF;
    float    m_dividerOpacity     = 0.18f;
    uint32_t m_signalColorHex     = 0xFFFFFFFF;
    float    m_signalDimOpacity   = 0.30f;

    // written by the RaknetUpdate hook, same pattern as PingCounterModule::m_ping
    int m_ping = 0;

private:
    bool  m_patched = false;
    void* m_patchTarget = nullptr;

    bool m_hasLastFrame = false;
    std::chrono::steady_clock::time_point m_lastFrameTime;
    float m_accumSeconds = 0.f;
    int   m_accumFrameCount = 0;
    int   m_displayFps = 0;
    float m_displayFrameMs = 0.f;
};
