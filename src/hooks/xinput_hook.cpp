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

    // While every button of ModActivator's controller shortcut is held, those
    // buttons are kept from the game, so the press that completes it (and the
    // hold that follows) does not also act in the game.
    static void MaskShortcut(XINPUT_STATE* s)
    {
        const unsigned mask = State::Get().padShortcut.load();
        if (!mask)
            return;
        auto& pad = s->Gamepad;
        const unsigned held = pad.wButtons |
            (pad.bLeftTrigger >= 64 ? (1u << 16) : 0) | (pad.bRightTrigger >= 64 ? (1u << 17) : 0);
        if ((held & mask) != mask)
            return;
        pad.wButtons &= static_cast<WORD>(~mask);
        if (mask & (1u << 16)) pad.bLeftTrigger = 0;
        if (mask & (1u << 17)) pad.bRightTrigger = 0;
    }

    // A distinct detour per module so each can call the matching trampoline
    // (MinHook can't tell us which target a shared detour was invoked for).
    #define ML_XINPUT_DETOUR(NAME, ORIG)                              \
        static DWORD WINAPI NAME(DWORD i, XINPUT_STATE* s)                 \
        {                                                                  \
            const DWORD r = ORIG(i, s);                                    \
            if (r == ERROR_SUCCESS && s)                                   \
            {                                                              \
                if (State::Get().Captures())                               \
                    Neutralize(s);                                         \
                else                                                       \
                    MaskShortcut(s);                                       \
            }                                                              \
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

    // Our own reads go to the trampolines, which reach XInput without the
    // detours; without hooks (HookXInput=0, or before they are in), straight
    // to the module's export, which is then not detoured either.
    static XInputGetState_t RawGetState()
    {
        if (o_1_4) return o_1_4;
        if (o_1_3) return o_1_3;
        if (o_9_1_0) return o_9_1_0;
        static XInputGetState_t direct = nullptr;
        static bool looked = false;
        if (!looked)
        {
            looked = true;
            HMODULE mod = GetModuleHandleW(L"xinput1_4.dll");
            if (!mod) mod = GetModuleHandleW(L"xinput1_3.dll");
            if (!mod) mod = LoadLibraryW(L"xinput1_4.dll");
            if (mod) direct = reinterpret_cast<XInputGetState_t>(GetProcAddress(mod, "XInputGetState"));
        }
        // Once our detour is in, the checks above find its trampoline first.
        return direct;
    }

    bool ReadPadRaw(XINPUT_GAMEPAD* out)
    {
        static int slot = -1;     // the connected controller, or -1
        static DWORD nextLook = 0;
        const auto get = RawGetState();
        if (!get)
            return false;
        XINPUT_STATE state = {};
        if (slot >= 0)
        {
            if (get(static_cast<DWORD>(slot), &state) == ERROR_SUCCESS)
            {
                *out = state.Gamepad;
                return true;
            }
            slot = -1;
            nextLook = 0;
        }
        const DWORD now = GetTickCount();
        if (nextLook && static_cast<LONG>(now - nextLook) < 0)
            return false;
        nextLook = now + 2000;
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
            if (get(i, &state) == ERROR_SUCCESS)
            {
                slot = static_cast<int>(i);
                *out = state.Gamepad;
                return true;
            }
        return false;
    }
}
