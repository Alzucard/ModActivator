#pragma once

namespace ml
{
    // [General] options from ModActivator.ini beside the plugin, read once at startup.
    struct Config
    {
        int wrapSwapChain = -1;     // WrapSwapChain: 1 always, 0 never, -1 auto (see WrapWanted)
        bool enableDred = false;    // EnableDred
        bool hookXInput = true;     // HookXInput: hold the controller back from the game while the menu is open
        bool debugInput = false;    // DebugInput
    };
    namespace Settings
    {
        inline Config& Get() { static Config c; return c; }
    }
}
