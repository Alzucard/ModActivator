#pragma once

namespace ml
{
    // [General] options from ModActivator.ini, read once at startup.
    struct Config
    {
        bool wrapSwapChain = false; // WrapSwapChain
        bool enableDred = false;    // EnableDred
        bool hookXInput = true;     // HookXInput: hold the controller back from the game while the menu is open
        bool debugInput = false;    // DebugInput
    };
    namespace Settings
    {
        inline Config& Get() { static Config c; return c; }
    }
}
