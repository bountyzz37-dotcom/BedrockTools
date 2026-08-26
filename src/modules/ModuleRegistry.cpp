#include "ModuleRegistry.hpp"

#include "Module.hpp"

// Player modules
#include "player/weatherchanger.hpp"
#include "player/timechanger.hpp"
#include "player/autoreq.hpp"
#include "player/autogg.hpp"
#include "player/skinstealer.hpp"
#include "player/nick.hpp"

// HUD modules
#include "hud/speeddisplay.hpp"
#include "hud/compass.hpp"
#include "hud/keystrokes.hpp"
#include "hud/playercoords.hpp"
#include "hud/infooverlay.hpp"
#include "hud/tablist.hpp"
#include "hud/targethud.hpp"

namespace bedrocktools {

ModuleRegistry::ModuleRegistry() {

    // ------------------------------------------------------------
    // Player modules
    // ------------------------------------------------------------

    registry.emplace<WeatherChangerModule>();
    registry.emplace<TimeChangerModule>();
    registry.emplace<AutoReqModule>();
    registry.emplace<AutoGGModule>();
    registry.emplace<SkinStealerModule>();
    registry.emplace<NickModule>();


    // ------------------------------------------------------------
    // HUD modules
    // ------------------------------------------------------------

    registry.emplace<SpeedDisplayModule>();
    registry.emplace<CompassModule>();
    registry.emplace<KeystrokesModule>();
    registry.emplace<PlayerCoordsModule>();

    // New Info Overlay
    registry.emplace<InfoOverlayModule>();

    registry.emplace<TabListModule>();
}

} // namespace bedrocktools
