#pragma once

#include "../Module.hpp"

#include <chrono>
#include <string>

class InfoOverlayModule : public Module {
public:
    InfoOverlayModule();
    ~InfoOverlayModule() override;

    void onInit() override;
    void onEnable() override;
    void onDisable() override;
    void onFrame() override;

    void loadConfig(const nlohmann::json& j) override;
    void saveConfig(nlohmann::json& j) override;

private:
    void updateFps();
    std::string getTimeString() const;
    std::string getSignalBars() const;

    int m_fps = 0;
    int m_ping = 0;

    std::chrono::steady_clock::time_point m_lastFrameTime;
    std::chrono::steady_clock::time_point m_fpsTimer;

    int m_frameCounter = 0;

    float hudPosX = 160.0f;
    float hudPosY = 20.0f;

    float m_textSize = 32.0f;

    bool m_background = true;
    float m_backgroundOpacity = 0.78f;

    bool m_showLogo = true;
    bool m_showFps = true;
    bool m_showPing = true;
    bool m_showTime = true;
};
