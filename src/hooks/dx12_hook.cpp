// Adapted for Master Looter from Trinity (https://github.com/XeTrinityz/Trinity),
// MIT License, Copyright (c) 2026 XeTrinityz. See THIRD_PARTY_NOTICES.md.
// Changes: Master Looter namespaces, logger, settings and menu hooks; icon atlas code removed.
#include "dx12_hook.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include "input.h"
#include "reshade_events.h"
#include "xinput_hook.h"
#include "hdr_composite_shader.h"
#include "../core/log.h"
#include "../core/settings.h"
#include "../core/state.h"
#include "../gui/menu.h"
#include "farhook.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace ml::hooks
{
// The overlay's DirectX 12 side, in parts under dx12/ that are included here,
// in this order, into this one file: each relies on the ones before it, and
// everything stays internal to this file as when it was a single source.
// Installing it all (InstallDX12Hooks) is at the end of this file.
#include "dx12/state.h"

#include "dx12/resources.h"

#include "dx12/draw.h"

#include "dx12/present_hooks.h"

#include "dx12/hook_owners.h"

#include "dx12/swapchain_wrapper.h"

#include "dx12/install.h"

#include "dx12/creation_patch.h"

    // Arm DRED so a later device removal is diagnosable. MUST run before the game
    // creates its D3D12 device - we do, because the ASI loader (winmm.dll) injects
    // us at process start, well ahead of the engine's renderer init. Global process
    // setting: applies to the device the game subsequently creates. Cheap; the
    // breadcrumb ring adds negligible overhead. See DumpDred.
    static void EnableDredIfAvailable()
    {
        ID3D12DeviceRemovedExtendedDataSettings* dred = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))) && dred)
        {
            dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->Release();
            LOG("DRED armed - a device removal will report GPU breadcrumbs + page faults.");
        }
        else
        {
            LOG("DRED unavailable on this system - device-removed causes stay opaque.");
        }
    }

    static HMODULE FindReShade()
    {
        HMODULE modules[512]{};
        DWORD bytes = 0;
        if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof modules, &bytes))
            return nullptr;
        const auto count = (std::min)(bytes, static_cast<DWORD>(sizeof modules)) /
                           sizeof(HMODULE);
        for (size_t i = 0; i < count; ++i)
            if (GetProcAddress(modules[i], "ReShadeRegisterAddon"))
                return modules[i];
        return nullptr;
    }

    bool InstallDX12Hooks(HMODULE self, bool useReShadeEvents)
    {
        // No throwaway device, ever. This used to create a D3D12 device, a
        // command queue and a swapchain purely to read vtable slots, then
        // release them. ReShade redirects every D3D12CreateDevice, and RenoDX
        // attaches to the first device it sees, which was ours: on the test
        // machine its only OnInitDevice of the whole run hooked that device,
        // we released it, and it never re-attached to the game's real one.
        // Its injections then ran against nothing and the game died at
        // CrimsonDesert.exe+0x30314D2, which is issue 34 exactly.
        //
        // The vtable slots are read instead from the game's own swapchain and
        // queue the first time it creates them, inside the factory hook, and
        // the detours go in then. Same addresses on a clean machine, because
        // vtables are per class; on a machine with a proxy in front, the
        // proxy's own vtable, which the ownership check then refuses to touch.
        if (Settings::Get().enableDred) EnableDredIfAvailable();
        else LOG("DRED not armed (EnableDred=0). Set EnableDred=1 to report GPU breadcrumbs on a device removal.");

        InitializeCriticalSection(&g_queueLock);
        g_queueLockReady = true;

        if (HMODULE reshade = useReShadeEvents ? FindReShade() : nullptr)
        {
            const auto registerAddon = reinterpret_cast<reshade_api::RegisterAddon>(
                GetProcAddress(reshade, "ReShadeRegisterAddon"));
            const auto registerEvent = reinterpret_cast<reshade_api::RegisterEvent>(
                GetProcAddress(reshade, "ReShadeRegisterEventForAddon"));
            if (registerAddon && registerEvent && registerAddon(self, reshade_api::version))
            {
                registerEvent(self, reshade_api::destroySwapchain,
                              reinterpret_cast<void*>(&OnReShadeDestroySwapChain));
                registerEvent(self, reshade_api::present, reinterpret_cast<void*>(&OnReShadePresent));
                registerEvent(self, reshade_api::reshadePresent,
                              reinterpret_cast<void*>(&OnReShadeEffectsPresent));
                LOG("Using ReShade presentation callbacks. No DXGI hooks installed.");
                return true;
            }
            // ReShade before 6.8.0 has an older add-on API and refuses this add-on,
            // and some builds lack the event export. Either way the legacy
            // path below still works and stacks on ReShade's dxgi.dll proxy,
            // so fall through rather than leave the menu dead (issue #1).
            LOG_ERR("ReShade add-on API v%u registration failed (ReShade 6.8.0 or newer is needed for it). "
                    "Using the legacy DirectX path instead. Set UseReShadeEvents=0 to skip this attempt.",
                    reshade_api::version);
        }

        // The factory detour is always installed: it is where the game's real
        // swapchain and queue first appear, and so the only place the other
        // detour targets can be read from. Whether the chain is then wrapped
        // is decided inside it, from WrapSwapChain.
        InstallSwapChainCreationPatch();
        // OverlayBlindWatch only starts once the creation detour has run, so
        // it cannot report the case where it never runs at all. This one
        // starts now and, if a minute passes without a single frame reaching
        // any path, writes who owns each way the game can make a swapchain.
        if (HANDLE watch = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                for (int i = 0; i < 300 && g_presentSeen.load(std::memory_order_relaxed) == 0; ++i)
                    Sleep(200);
                if (g_presentSeen.load(std::memory_order_relaxed) != 0)
                    return 0;
                LOG_ERR("[overlay] a minute after start no frame has reached this mod (creation detour ran %ld time(s)), so the menu cannot open. "
                        "The [creation] lines below show where the game's swapchain creation goes instead.",
                        g_factoryCalls.load(std::memory_order_relaxed));
                LogSwapChainCreationPath();
                return 0;
            }, nullptr, 0, nullptr))
            CloseHandle(watch);

        char exePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        const char* exeName = strrchr(exePath, '\\');
        exeName = exeName ? exeName + 1 : exePath;
        LOG("DX12 hooks installed (%s, pid %lu).", exeName, GetCurrentProcessId());
        return true;
    }
}
