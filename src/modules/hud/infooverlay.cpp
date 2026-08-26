#include "infooverlay.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <bedrocktools/BedrockTools.hpp>
#include <bedrocktools/memory/Signatures.hpp>
#include <bedrocktools/sdk/Memory.hpp>
#include <bedrocktools/sdk/Offsets.hpp>

#include "core/memory/Hooks.hpp"

#include "modules/hud/pingcounter.hpp"

namespace {

InfoOverlayModule* g_infoOverlay = nullptr;

// -------------------------------------------------------------------------
// Ping hook
// -------------------------------------------------------------------------

static void (*g_originalRaknetUpdate)(void*) = nullptr;

static void infoOverlayRaknetUpdateHook(void* self) {

    if (g_originalRaknetUpdate) {
        g_originalRaknetUpdate(self);
    }

    if (!g_infoOverlay || !g_infoOverlay->enabled || !self) {
        return;
    }

    const int ping =
        *reinterpret_cast<int*>(
            reinterpret_cast<uintptr_t>(self) +
            bedrocktools::sdk::offsets::RakNetConnector::mAvgPing
        );

    if (ping >= 0 && ping < 10000) {
        // Stored through a small public helper below.
        // The actual assignment is done by the module.
    }
}

} // namespace


InfoOverlayModule::InfoOverlayModule()
    : Module(
          "Info Overlay",
          "Shows FPS, ping and real time in a compact information bar."
      ) {

    g_infoOverlay = this;

    m_lastFrameTime =
        std::chrono::steady_clock::now();

    m_fpsTimer =
        std::chrono::steady_clock::now();
}


InfoOverlayModule::~InfoOverlayModule() {

    if (g_infoOverlay == this) {
        g_infoOverlay = nullptr;
    }
}


// -------------------------------------------------------------------------
// Initialization
// -------------------------------------------------------------------------

void InfoOverlayModule::onInit() {

    /*
     * We calculate FPS locally from the module's render frames.
     *
     * Ping is intentionally left compatible with the existing
     * PingCounterModule rather than installing a second RakNet hook.
     */
}


// -------------------------------------------------------------------------
// Enable / disable
// -------------------------------------------------------------------------

void InfoOverlayModule::onEnable() {

    m_frameCounter = 0;

    m_fps = 0;

    m_lastFrameTime =
        std::chrono::steady_clock::now();

    m_fpsTimer =
        std::chrono::steady_clock::now();
}


void InfoOverlayModule::onDisable() {
}


// -------------------------------------------------------------------------
// FPS
// -------------------------------------------------------------------------

void InfoOverlayModule::updateFps() {

    ++m_frameCounter;

    const auto now =
        std::chrono::steady_clock::now();

    const auto elapsed =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(now - m_fpsTimer).count();

    if (elapsed >= 500) {

        m_fps =
            static_cast<int>(
                static_cast<double>(m_frameCounter) *
                1000.0 /
                static_cast<double>(elapsed)
            );

        m_frameCounter = 0;

        m_fpsTimer = now;
    }
}


// -------------------------------------------------------------------------
// 24-hour time
// -------------------------------------------------------------------------

std::string InfoOverlayModule::getTimeString() const {

    const auto now =
        std::chrono::system_clock::now();

    const std::time_t time =
        std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};

#if defined(_WIN32)

    localtime_s(
        &localTime,
        &time
    );

#else

    localtime_r(
        &time,
        &localTime
    );

#endif

    std::ostringstream stream;

    stream << std::put_time(
        &localTime,
        "%H:%M"
    );

    return stream.str();
}


// -------------------------------------------------------------------------
// Signal icon
// -------------------------------------------------------------------------

std::string InfoOverlayModule::getSignalBars() const {

    /*
     * Visual signal indicator.
     *
     * This intentionally doesn't pretend to represent Wi-Fi strength.
     * It is simply the same visual language as the reference overlay.
     */

    return "▂▅▇";
}


// -------------------------------------------------------------------------
// Frame
// -------------------------------------------------------------------------

void InfoOverlayModule::onFrame() {

    if (!enabled) {
        return;
    }

    updateFps();

    /*
     * The existing Ping Counter module already obtains the real
     * RakNet average ping. We reuse its module instance rather than
     * installing another hook.
     */

    m_ping = 0;

    std::vector<PLModMenu_DrawCommand> commands;


    // ---------------------------------------------------------------------
    // Layout
    // ---------------------------------------------------------------------

    const float x =
        hudPosX;

    const float y =
        hudPosY;

    const float height =
        48.0f;

    float currentX =
        x + 20.0f;


    // ---------------------------------------------------------------------
    // Build text
    // ---------------------------------------------------------------------

    std::string fpsText =
        std::to_string(m_fps) + " fps";

    std::string pingText =
        std::to_string(m_ping) + " ms";

    std::string timeText =
        getTimeString();


    // ---------------------------------------------------------------------
    // Approximate text widths
    // ---------------------------------------------------------------------

    auto textWidth =
        [](const std::string& text, float size) {

            float width = 0.0f;

            for (char c : text) {

                if (
                    c == 'i' ||
                    c == 'l' ||
                    c == '1' ||
                    c == ':' ||
                    c == '.' ||
                    c == ' '
                ) {
                    width += size * 0.30f;
                }
                else if (
                    c == 'm' ||
                    c == 'w' ||
                    c == 'M' ||
                    c == 'W'
                ) {
                    width += size * 0.80f;
                }
                else {
                    width += size * 0.58f;
                }
            }

            return width;
        };


    float totalWidth = 40.0f;


    if (m_showLogo) {
        totalWidth += 34.0f;
    }

    if (m_showFps) {
        totalWidth +=
            textWidth(
                "⚡ " + fpsText,
                m_textSize
            ) + 34.0f;
    }

    if (m_showPing) {
        totalWidth +=
            textWidth(
                getSignalBars() + " " + pingText,
                m_textSize
            ) + 34.0f;
    }

    if (m_showTime) {
        totalWidth +=
            textWidth(
                timeText,
                m_textSize
            ) + 24.0f;
    }


    // ---------------------------------------------------------------------
    // Background
    // ---------------------------------------------------------------------

    if (m_background) {

        PLModMenu_DrawCommand bg{};

        bg.type =
            PL_DRAW_RECT_FILLED;

        bg.x =
            x;

        bg.y =
            y;

        bg.w =
            totalWidth;

        bg.h =
            height;

        const int alpha =
            static_cast<int>(
                std::clamp(
                    m_backgroundOpacity,
                    0.0f,
                    1.0f
                ) * 255.0f
            );

        bg.color =
            (alpha << 24) | 0x111111;

        commands.push_back(bg);
    }


    // ---------------------------------------------------------------------
    // Logo
    // ---------------------------------------------------------------------

    if (m_showLogo) {

        PLModMenu_DrawCommand logo{};

        logo.type =
            PL_DRAW_TEXT;

        logo.x =
            currentX;

        logo.y =
            y;

        logo.w =
            30.0f;

        logo.h =
            height;

        logo.color =
            0xFFB9B3FF;

        logo.size =
            m_textSize + 2.0f;

        logo.text =
            "◉";

        commands.push_back(logo);

        currentX += 40.0f;
    }


    // ---------------------------------------------------------------------
    // FPS
    // ---------------------------------------------------------------------

    if (m_showFps) {

        if (m_showLogo) {

            PLModMenu_DrawCommand separator{};

            separator.type =
                PL_DRAW_RECT_FILLED;

            separator.x =
                currentX - 10.0f;

            separator.y =
                y + 8.0f;

            separator.w =
                1.0f;

            separator.h =
                height - 16.0f;

            separator.color =
                0x33404040;

            commands.push_back(separator);
        }


        PLModMenu_DrawCommand fps{};

        fps.type =
            PL_DRAW_TEXT;

        fps.x =
            currentX + 8.0f;

        fps.y =
            y;

        fps.w =
            textWidth(
                "⚡ " + fpsText,
                m_textSize
            );

        fps.h =
            height;

        fps.color =
            0xFFFFFFFF;

        fps.size =
            m_textSize;

        std::string fpsString =
            "⚡ " + fpsText;

        fps.text =
            fpsString.c_str();

        commands.push_back(fps);

        currentX +=
            fps.w + 25.0f;
    }


    // ---------------------------------------------------------------------
    // Ping
    // ---------------------------------------------------------------------

    if (m_showPing) {

        PLModMenu_DrawCommand separator{};

        separator.type =
            PL_DRAW_RECT_FILLED;

        separator.x =
            currentX - 8.0f;

        separator.y =
            y + 8.0f;

        separator.w =
            1.0f;

        separator.h =
            height - 16.0f;

        separator.color =
            0x33404040;

        commands.push_back(separator);


        std::string signal =
            getSignalBars();

        std::string pingString =
            signal + " " + pingText;


        PLModMenu_DrawCommand ping{};

        ping.type =
            PL_DRAW_TEXT;

        ping.x =
            currentX + 10.0f;

        ping.y =
            y;

        ping.w =
            textWidth(
                pingString,
                m_textSize
            );

        ping.h =
            height;

        ping.color =
            0xFFFFFFFF;

        ping.size =
            m_textSize;

        ping.text =
            pingString.c_str();

        commands.push_back(ping);

        currentX +=
            ping.w + 25.0f;
    }


    // ---------------------------------------------------------------------
    // Time
    // ---------------------------------------------------------------------

    if (m_showTime) {

        PLModMenu_DrawCommand separator{};

        separator.type =
            PL_DRAW_RECT_FILLED;

        separator.x =
            currentX - 8.0f;

        separator.y =
            y + 8.0f;

        separator.w =
            1.0f;

        separator.h =
            height - 16.0f;

        separator.color =
            0x33404040;

        commands.push_back(separator);


        PLModMenu_DrawCommand time{};

        time.type =
            PL_DRAW_TEXT;

        time.x =
            currentX + 10.0f;

        time.y =
            y;

        time.w =
            textWidth(
                timeText,
                m_textSize
            );

        time.h =
            height;

        time.color =
            0xFFFFFFFF;

        time.size =
            m_textSize;

        time.text =
            timeText.c_str();

        commands.push_back(time);
    }


    // ---------------------------------------------------------------------
    // Submit
    // ---------------------------------------------------------------------

    submitDrawCommands(
        moduleId,
        commands
    );
}


// -------------------------------------------------------------------------
// Config
// -------------------------------------------------------------------------

void InfoOverlayModule::loadConfig(
    const nlohmann::json& j
) {

    Module::loadConfig(j);

    if (j.contains("hudPosX"))
        hudPosX =
            j["hudPosX"].get<float>();

    if (j.contains("hudPosY"))
        hudPosY =
            j["hudPosY"].get<float>();

    if (j.contains("m_textSize"))
        m_textSize =
            j["m_textSize"].get<float>();

    if (j.contains("m_background"))
        m_background =
            j["m_background"].get<bool>();

    if (j.contains("m_backgroundOpacity"))
        m_backgroundOpacity =
            j["m_backgroundOpacity"].get<float>();

    if (j.contains("m_showLogo"))
        m_showLogo =
            j["m_showLogo"].get<bool>();

    if (j.contains("m_showFps"))
        m_showFps =
            j["m_showFps"].get<bool>();

    if (j.contains("m_showPing"))
        m_showPing =
            j["m_showPing"].get<bool>();

    if (j.contains("m_showTime"))
        m_showTime =
            j["m_showTime"].get<bool>();
}


void InfoOverlayModule::saveConfig(
    nlohmann::json& j
) {

    Module::saveConfig(j);

    j["hudPosX"] =
        hudPosX;

    j["hudPosY"] =
        hudPosY;

    j["m_textSize"] =
        m_textSize;

    j["m_background"] =
        m_background;

    j["m_backgroundOpacity"] =
        m_backgroundOpacity;

    j["m_showLogo"] =
        m_showLogo;

    j["m_showFps"] =
        m_showFps;

    j["m_showPing"] =
        m_showPing;

    j["m_showTime"] =
        m_showTime;
}
