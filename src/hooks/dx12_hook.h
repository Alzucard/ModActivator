// Adapted for Master Looter from Trinity (https://github.com/XeTrinityz/Trinity),
// MIT License, Copyright (c) 2026 XeTrinityz. See THIRD_PARTY_NOTICES.md.
// Changes: Master Looter namespaces, logger, settings and menu hooks; icon atlas code removed.
#pragma once
#include <Windows.h>

namespace ml::hooks
{
    // Uses ReShade callbacks when available, otherwise installs the DX12 hooks.
    // Must be called after MH_Initialize().
    bool InstallDX12Hooks(HMODULE self, bool useReShadeEvents);

    // Written when no frame has arrived in a minute: who owns each way
    // the game's factory can make a swapchain, and whether this mod's
    // creation detour is still in line and has ever run.
    void LogSwapChainCreationPath();
}
