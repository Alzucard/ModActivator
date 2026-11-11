// Adapted for Master Looter from Trinity (https://github.com/XeTrinityz/Trinity),
// MIT License, Copyright (c) 2026 XeTrinityz. See THIRD_PARTY_NOTICES.md.
// Changes: Master Looter namespaces, logger, settings and menu hooks; icon atlas code removed.
#include "xinput_hook.h"

#include "../core/log.h"
#include "../core/settings.h"

#include <MinHook.h>

#include "../core/state.h"

namespace ml::hooks
{
    using XInputGetState_t = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);

    // One trampoline per known module. The game and our own overlay may link
    // different xinput versions, and each hook must forward to its own original.
    static XInputGetState_t o_1_4   = nullptr;
    static XInputGetState_t o_1_3   = nullptr;
    static XInputGetState_t o_9_1_0 = nullptr;

    static void Neutralize(XINPUT_STATE* s)
    {
        const DWORD packet = s->dwPacketNumber;
        ZeroMemory(&s->Gamepad, sizeof s->Gamepad);
        s->dwPacketNumber = packet + 1;
    }

    // A distinct detour per module so each can call the matching trampoline
    // (MinHook can't tell us which target a shared detour was invoked for).
    #define ML_XINPUT_DETOUR(NAME, ORIG)                              \
        static DWORD WINAPI NAME(DWORD i, XINPUT_STATE* s)                 \
        {                                                                  \
            const DWORD r = ORIG(i, s);                                    \
            if (r == ERROR_SUCCESS && s && State::Get().Captures())       \
                Neutralize(s);                                             \
            return r;                                                      \
        }
    ML_XINPUT_DETOUR(hk_1_4,   o_1_4)
    ML_XINPUT_DETOUR(hk_1_3,   o_1_3)
    ML_XINPUT_DETOUR(hk_9_1_0, o_9_1_0)
    #undef ML_XINPUT_DETOUR

    struct Target
    {
        const wchar_t*    dll;
        void*             detour;
        XInputGetState_t* original;
        bool              done; // hooked, or confirmed nothing to hook here
    };

    static Target g_targets[] = {
        { L"xinput1_4.dll",   reinterpret_cast<void*>(&hk_1_4),   &o_1_4,   false },
        { L"xinput1_3.dll",   reinterpret_cast<void*>(&hk_1_3),   &o_1_3,   false },
        { L"xinput9_1_0.dll", reinterpret_cast<void*>(&hk_9_1_0), &o_9_1_0, false },
    };

    void EnsureXInputHooks()
    {
        static bool s_allDone = false;
        if (s_allDone)
            return;

        // Nothing at all when it is switched off: no detour on any XInput
        // module, no polling for shortcuts, the pad untouched.
        if (!Settings::Get().hookXInput)
        {
            static bool s_said = false;
            if (!s_said)
            {
                s_said = true;
                LOG("HookXInput=0: the controller is left alone, so it still reaches the game "
                    "while the menu is open.");
            }
            s_allDone = true;
            return;
        }

        bool anyPending = false;
        for (auto& t : g_targets)
        {
            if (t.done)
                continue;

            HMODULE mod = GetModuleHandleW(t.dll);
            if (!mod)
            {
                anyPending = true; // may still load (game inits input after us)
                continue;
            }

            FARPROC proc = GetProcAddress(mod, "XInputGetState");
            if (!proc)
            {
                t.done = true; // module present but no export - never retry it
                continue;
            }

            if (MH_CreateHook(reinterpret_cast<void*>(proc), t.detour,
                              reinterpret_cast<void**>(t.original)) != MH_OK)
            {
                anyPending = true; // transient; try again next frame
                continue;
            }

            if (MH_EnableHook(reinterpret_cast<void*>(proc)) == MH_OK)
            {
                t.done = true;
            }
            else
            {
                anyPending = true;
            }
        }

        if (!anyPending)
            s_allDone = true;
    }
}
